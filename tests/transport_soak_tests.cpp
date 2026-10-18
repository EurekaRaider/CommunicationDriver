#include <communicationdriver/communicationdriver.hpp>
#include <boost/asio.hpp>
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

namespace asio = boost::asio;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

static std::mutex log_mutex;

static void log(const std::string& text)
{
    std::lock_guard lock(log_mutex);
    std::cout << text << std::endl;
}

static std::string quote(const std::string& s)
{
    std::string out = "'";
    for (char c : s)
        out += c == '\'' ? "'\\''" : std::string(1, c);
    return out + "'";
}

static std::string env(const char* name)
{
    auto p = std::getenv(name);
    if (!p)
        throw std::runtime_error(std::string("Missing ") + name);
    return p;
}

static comm::CallOptions call(int ms = 10000)
{
    comm::CallOptions o;
    o.timeout = comm::Duration(ms);
    return o;
}

static std::string contents(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

struct Echo
{
    asio::io_context io;
    asio::executor_work_guard<asio::io_context::executor_type> guard = asio::make_work_guard(io);
    std::unique_ptr<asio::ip::tcp::acceptor> listener;
    std::vector<std::thread> threads;

    struct Tcp : std::enable_shared_from_this<Tcp>
    {
        asio::ip::tcp::socket socket;
        std::array<char, 65536> buffer{};

        explicit Tcp(asio::ip::tcp::socket s) : socket(std::move(s))
        {
        }

        void read()
        {
            auto self = shared_from_this();
            socket.async_read_some(asio::buffer(buffer),
                [self](auto e, size_t n)
                {
                    if (e)
                        return;

                    asio::async_write(self->socket, asio::buffer(self->buffer.data(), n),
                        [self](auto error, size_t)
                        {
                            if (!error)
                                self->read();
                        });
                });
        }
    };

    struct Udp
    {
        asio::ip::udp::socket socket;
        asio::ip::udp::endpoint sender;
        std::array<char, 65536> buffer{};

        explicit Udp(asio::io_context& io) : socket(io, {asio::ip::udp::v4(), 0})
        {
            read();
        }

        void read()
        {
            socket.async_receive_from(asio::buffer(buffer), sender,
                [this](auto e, size_t n)
                {
                    if (e)
                        return;

                    socket.async_send_to(asio::buffer(buffer.data(), n), sender,
                        [this](auto error, size_t)
                        {
                            if (!error)
                                read();
                        });
                });
        }
    };

    std::vector<std::unique_ptr<Udp>> datagrams;

    Echo(bool tcp, int count)
    {
        if (tcp)
        {
            listener = std::make_unique<asio::ip::tcp::acceptor>(
                io, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
            accept();
        }
        else
            for (int n = 0; n < count; ++n)
                datagrams.push_back(std::make_unique<Udp>(io));

        for (int n = 0; n < 2; ++n)
            threads.emplace_back(
                [this]
                {
                    io.run();
                });
    }

    void accept()
    {
        listener->async_accept(
            [this](auto e, auto s)
            {
                if (!e)
                {
                    boost::system::error_code ignored;
                    s.set_option(asio::ip::tcp::no_delay(true), ignored);
                    std::make_shared<Tcp>(std::move(s))->read();
                }
                if (listener->is_open())
                    accept();
            });
    }

    uint16_t port(int lane) const
    {
        return listener ? listener->local_endpoint().port()
                        : datagrams[lane]->socket.local_endpoint().port();
    }

    ~Echo()
    {
        io.stop();
        for (auto& t : threads)
            t.join();
    }
};

struct Counters
{
    std::atomic<uint64_t> commands{}, bulk{}, transfers{}, bytes{}, cancels{}, timeouts{},
        reopens{}, streams{}, checks{}, unknown_exit{}, max_us{};
};

struct Worker
{
    std::atomic<uint64_t> operations{};
    std::atomic<int64_t> last_ms{};
};

struct Test
{
    std::string kind;
    int seconds, workers;
    fs::path root;
    Counters counts;
    std::atomic<bool> stop = false, go = false;
    std::atomic<int> ready{};
    std::vector<std::shared_ptr<comm::Driver>> drivers;
    std::vector<std::future<void>> jobs;
    std::vector<std::shared_ptr<Worker>> states;
    std::unique_ptr<Echo> echo;
    Clock::time_point begin, deadline;
    std::mutex failures_mutex;
    std::vector<std::string> failures;

    Test(std::string k, int s, fs::path p, int w)
        : kind(std::move(k)), seconds(s), workers(w), root(fs::absolute(p))
    {
        fs::create_directories(root);
        if (kind == "tcp" || kind == "udp")
            echo = std::make_unique<Echo>(kind == "tcp", workers + 6);
        for (int n = 0; n < 4; ++n)
            drivers.push_back(std::make_shared<comm::Driver>());
    }

    void expect(bool yes, const std::string& reason)
    {
        ++counts.checks;
        if (!yes)
            throw std::runtime_error(reason);
    }

    comm::Connection connection(int lane, bool once = false, std::string session = {})
    {
        auto& d = *drivers[lane % drivers.size()];

        if (kind == "tcp")
        {
            comm::TcpOptions o;
            o.host = "127.0.0.1";
            o.port = echo->port(lane);
            o.max_response_bytes = 2 * 1024 * 1024;

            auto c = d.create_tcp(o);
            c.open();

            return c;
        }

        if (kind == "udp")
        {
            comm::UdpOptions o;
            o.remote_host = "127.0.0.1";
            o.remote_port = echo->port(lane);
            o.max_response_bytes = 2 * 1024 * 1024;

            auto c = d.create_udp(o);
            c.open();

            return c;
        }

        if (kind == "ssh")
        {
            comm::SshOptions o;
            o.host = "127.0.0.1";
            o.port = std::getenv("CD_SSH_PORT")
                         ? static_cast<uint16_t>(std::stoi(env("CD_SSH_PORT")))
                         : 22482;
            o.user_name = env("CD_SSH_USER");
            o.auth = comm::SshAuth::private_key(env("CD_SSH_PRIVATE_KEY"));
            o.host_key = comm::HostKey::sha256(env("CD_SSH_FINGERPRINT"));
            o.execution_mode =
                once ? comm::ExecutionMode::OneShot : comm::ExecutionMode::Persistent;
            o.session_command = std::move(session);

            auto c = d.create_ssh(o);
            c.open();

            return c;
        }

        comm::CliOptions o;
        o.execution_mode = once ? comm::ExecutionMode::OneShot : comm::ExecutionMode::Persistent;
#ifdef _WIN32
        o.shell = comm::ShellKind::Bash;
        if (!session.empty())
        {
            o.executable_path = env("CD_TEST_SH");
            o.arguments = {"-c", std::move(session)};
        }
#else
        if (!session.empty())
        {
            o.executable_path = "/bin/sh";
            o.arguments = {"-c", std::move(session)};
        }
#endif

        auto c = d.create_cli(o);
        c.open();

        return c;
    }

    template <class F>
    void launch(std::string label, F work)
    {
        auto state = std::make_shared<Worker>();
        states.push_back(state);
        jobs.push_back(std::async(std::launch::async,
            [this, label, state, work]
            {
                try
                {
                    work(state);
                }
                catch (const comm::CommunicationError& e)
                {
                    {
                        std::lock_guard lock(failures_mutex);
                        failures.push_back(label + ": " + e.what());
                    }
                    stop = true;
                    log("FAIL " + kind + " " + label +
                        ": native-status=" + std::to_string(e.info.code) + " " + e.what());
                }
                catch (const std::exception& e)
                {
                    {
                        std::lock_guard lock(failures_mutex);
                        failures.push_back(label + ": " + e.what());
                    }
                    stop = true;
                    log("FAIL " + kind + " " + label + ": " + e.what());
                }
            }));
    }

    bool start(const std::shared_ptr<Worker>& state)
    {
        ++ready;

        while (!go && !stop)
            std::this_thread::sleep_for(1ms);

        if (stop)
            return false;

        state->last_ms = 0;

        return true;
    }

    bool active() const
    {
        return !stop && Clock::now() < deadline;
    }

    void progress(const std::shared_ptr<Worker>& state)
    {
        ++state->operations;
        state->last_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - begin).count();
    }

    template <class F>
    void error(F work, cd_status_t code)
    {
        try
        {
            work();
            throw std::runtime_error("expected operation failure");
        }
        catch (const comm::CommunicationError& e)
        {
            expect(e.info.code == code, "operation status " + std::to_string(e.info.code) +
                                            " expected " + std::to_string(code));
        }
    }

    void network_workers()
    {
        for (int lane = 0; lane < workers; ++lane)
            launch("command-" + std::to_string(lane),
                [this, lane](auto state)
                {
                    auto c = connection(lane);

                    if (start(state))
                    {
                        uint64_t n = 0;
                        std::vector<std::byte> data(4096);

                        for (size_t i = 0; i < data.size(); ++i)
                            data[i] = std::byte((i * 31 + lane) % 256);

                        while (active())
                        {
                            auto tick = Clock::now();
                            auto text = "lane=" + std::to_string(lane) +
                                        " sequence=" + std::to_string(n++) +
                                        " utf8=" + "\xe4\xbd\xa0\xf0\x9f\x98\x84";
                            auto o = call();
                            o.receive_until = comm::ReceiveUntil::bytes(text.size());

                            auto r = c.execute(text, comm::Check::equals(text), o);
                            expect(r.response == text && r.check && r.check->passed,
                                "network text echo and binding");
                            ++counts.commands;
                            counts.bytes += text.size();
                            o.receive_until = comm::ReceiveUntil::bytes(data.size());

                            auto binary = c.execute(data, {}, o);
                            expect(binary.response == data, "network binary echo");
                            ++counts.commands;
                            counts.bytes += data.size();

                            auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                                Clock::now() - tick)
                                          .count();
                            auto prior = counts.max_us.load();

                            while (prior < uint64_t(us) &&
                                   !counts.max_us.compare_exchange_weak(prior, us))
                            {
                            }

                            progress(state);
                        }
                    }

                    c.close();
                });

        for (int lane = workers; lane < workers + 2; ++lane)
            launch("bulk-" + std::to_string(lane),
                [this, lane](auto state)
                {
                    auto c = connection(
                        lane); /* macOS maxdgram is 9216; keep each UDP datagram below the host limit. */
                    std::vector<std::byte> data(kind == "tcp" ? 1024 * 1024 : 8192);

                    for (size_t i = 0; i < data.size(); ++i)
                        data[i] = std::byte((i + lane) % 256);

                    if (start(state))
                        while (active())
                        {
                            c.write_bytes(data, call());

                            auto reply = c.read_exactly_bytes(data.size(), call());
                            expect(reply == data, "network continuous bulk equality");
                            ++counts.bulk;
                            counts.bytes += data.size();
                            progress(state);
                        }

                    c.close();
                });

        for (int lane = workers + 2; lane < workers + 6; ++lane)
            launch("cancel-" + std::to_string(lane),
                [this, lane](auto state)
                {
                    auto c = connection(lane);
                    uint64_t n = 0;

                    if (start(state))
                        while (active())
                        {
                            if (n++ % 2)
                            {
                                error(
                                    [&]
                                    {
                                        c.read_bytes(call(50));
                                    },
                                    CD_TIMEOUT);
                                ++counts.timeouts;
                            }
                            else
                            {
                                auto op = c.read_bytes_async(call());
                                std::this_thread::sleep_for(10ms);
                                op.cancel();
                                error(
                                    [&]
                                    {
                                        op.get();
                                    },
                                    CD_CANCELLED);
                                ++counts.cancels;
                            }

                            c.close();
                            c.open();
                            ++counts.reopens;
                            progress(state);
                        }

                    c.close();
                });
    }

    void command_workers()
    {
        for (int lane = 0; lane < workers; ++lane)
            launch("command-" + std::to_string(lane),
                [this, lane](auto state)
                {
                    auto c = connection(lane, lane % 4 == 3);
                    uint64_t n = 0;

                    if (start(state))
                        while (active())
                        {
                            auto token = "lane-" + std::to_string(lane) + "-" + std::to_string(n++);
                            auto r =
                                c.execute("printf " + quote(token) + "; printf ERR >&2; (exit 7)",
                                    {}, call());
                            expect(r.response == token && r.standard_error == "ERR" &&
                                       r.exit_code == 7,
                                "parallel command identity and nonzero exit");
                            ++counts.commands;

                            if (n % 32 == 0)
                            {
                                auto early =
                                    c.execute_until("printf READY; sleep 0.02; printf TAIL",
                                        comm::Check::contains("READY"), call());
                                expect(early.completion_reason == comm::CompletionReason::Matched,
                                    "early match");

                                auto next = c.execute("printf NEXT", {}, call());
                                expect(next.response == "NEXT" && next.standard_error.empty() &&
                                           next.completion_reason ==
                                               comm::CompletionReason::CommandCompleted,
                                    "tail cleanup ownership");

                                if (!next.exit_code)
                                {
                                    expect(kind == "ssh" && lane % 4 == 3,
                                        "only SSH exec may have unknown zero status in libssh2 1.11");
                                    ++counts.unknown_exit;
                                }
                                else
                                    expect(next.exit_code == 0, "successful next command exit");

                                counts.commands += 2;
                            }

                            progress(state);
                        }

                    c.close();
                });

        for (int lane = workers; lane < workers + 2; ++lane)
            launch("stream-" + std::to_string(lane),
                [this, lane](auto state)
                {
                    auto token = "STREAM-" + std::to_string(lane);
                    auto c = connection(lane, false,
                        "while :; do printf '" + token + "\\n'; printf 'ERR-" + token +
                            "\\n' >&2; sleep 0.02; done");
                    auto err = call();
                    err.read_stream = comm::Stream::StandardError;

                    if (start(state))
                        while (active())
                        {
                            expect(c.read_line(call()) == token, "continuous stdout");
                            expect(c.read_line(err) == "ERR-" + token, "continuous stderr");
                            counts.streams += 2;
                            progress(state);
                        }

                    c.close();
                });

        for (int lane = workers + 2; lane < workers + 6; ++lane)
            launch("cancel-" + std::to_string(lane),
                [this, lane](auto state)
                {
                    auto c = connection(lane);
                    uint64_t n = 0;

                    if (start(state))
                        while (active())
                        {
                            if (n++ % 2)
                            {
                                error(
                                    [&]
                                    {
                                        c.execute(
                                            "printf PART; while IFS= read -r __cd_wait; do :; done",
                                            {}, call(50));
                                    },
                                    CD_TIMEOUT);
                                ++counts.timeouts;
                            }
                            else
                            {
                                auto op = c.execute_async(
                                    "printf PART; while IFS= read -r __cd_wait; do :; done", {},
                                    call());
                                std::this_thread::sleep_for(50ms);
                                op.cancel();
                                error(
                                    [&]
                                    {
                                        op.get();
                                    },
                                    CD_CANCELLED);
                                ++counts.cancels;
                            }

                            c.close();
                            c.open();

                            auto r = c.execute("printf RECOVERED", {}, call());
                            expect(r.response == "RECOVERED", "cancel/timeout recovery output");
                            ++counts.reopens;
                            progress(state);
                        }

                    c.close();
                });

        if (kind == "ssh")
            for (int lane = 0; lane < 4; ++lane)
                launch("sftp-" + std::to_string(lane),
                    [this, lane](auto state)
                    {
                        auto c = connection(lane);
                        auto local = root / ("source-" + std::to_string(lane) + ".bin"),
                             download = root / ("download-" + std::to_string(lane) + ".bin");
                        auto remote =
                            std::getenv("CD_SSH_REMOTE_ROOT")
                                ? env("CD_SSH_REMOTE_ROOT") + "/file-" + std::to_string(lane) +
                                      ".bin"
                                : (root / "remote" / ("file-" + std::to_string(lane) + ".bin"))
                                      .string();
                        std::string data(1024 * 1024, '\0');
                        uint32_t x = 0x12345678 + lane;

                        for (auto& ch : data)
                        {
                            x ^= x << 13;
                            x ^= x >> 17;
                            x ^= x << 5;
                            ch = char(x);
                        }

                        std::ofstream(local, std::ios::binary).write(data.data(), data.size());

                        comm::TransferOptions o;
                        o.timeout = comm::Duration(10000);

                        if (start(state))
                            while (active())
                            {
                                auto up = c.upload(local.string(), remote, o),
                                     down = c.download(remote, download.string(), o);
                                expect(up.bytes_transferred == data.size() &&
                                           down.bytes_transferred == data.size() &&
                                           contents(download) == data,
                                    "concurrent SFTP byte equality and progress");
                                counts.transfers += 2;
                                counts.bytes += 2 * data.size();
                                progress(state);
                            }

                        c.close();
                    });
    }

    int run()
    {
        fs::create_directories(root / "remote");

        if (echo)
            network_workers();
        else
            command_workers();

        auto startup = Clock::now();

        while (!stop && ready < int(jobs.size()))
        {
            if (Clock::now() - startup > 60s)
            {
                stop = true;
                log("FAIL startup did not complete");
                break;
            }
            std::this_thread::sleep_for(10ms);
        }

        begin = Clock::now();
        deadline = begin + std::chrono::seconds(seconds);
        go = true;
        log("START protocol=" + kind + " workers=" + std::to_string(jobs.size()) +
            " drivers=4 seconds=" + std::to_string(seconds));

        while (!stop && Clock::now() < deadline)
        {
            std::this_thread::sleep_for(1s);

            auto ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - begin).count();

            for (auto& state : states)
                if (ms - state->last_ms > 60000)
                {
                    stop = true;
                    log("FAIL worker stopped advancing");
                }

            if (ms / 1000 % 10 == 0)
                log("PROGRESS protocol=" + kind + " seconds=" + std::to_string(ms / 1000) +
                    " commands=" + std::to_string(counts.commands) + " bulk=" +
                    std::to_string(counts.bulk) + " transfers=" + std::to_string(counts.transfers) +
                    " bytes=" + std::to_string(counts.bytes) + " streams=" +
                    std::to_string(counts.streams) + " reopens=" + std::to_string(counts.reopens));
        }

        bool completed = !stop;
        stop = true;

        for (auto& job : jobs)
            job.get();

        for (auto& d : drivers)
            d->close();

        expect(completed && failures.empty(),
            failures.empty() ? "full duration did not complete" : failures.front());

        for (auto& state : states)
            expect(state->operations > 0, "every worker must perform real IO");

        log("SUMMARY protocol=" + kind + " seconds=" + std::to_string(seconds) + " workers=" +
            std::to_string(jobs.size()) + " commands=" + std::to_string(counts.commands) +
            " bulk=" + std::to_string(counts.bulk) + " transfers=" +
            std::to_string(counts.transfers) + " bytes=" + std::to_string(counts.bytes) +
            " streams=" + std::to_string(counts.streams) + " cancels=" +
            std::to_string(counts.cancels) + " timeouts=" + std::to_string(counts.timeouts) +
            " reopens=" + std::to_string(counts.reopens) + " checks=" +
            std::to_string(counts.checks) + " unknown_exit=" + std::to_string(counts.unknown_exit) +
            " max_command_pair_us=" + std::to_string(counts.max_us));
        log("PASS " + kind + " full-duration mixed IO");

        return 0;
    }
};

int main(int argc, char** argv)
{
    try
    {
        if (argc < 4)
            throw std::runtime_error(
                "Use: <tcp|udp|cli|ssh> <seconds> <artifact-root> [command-workers]");

        std::string kind = argv[1];

        if (kind != "tcp" && kind != "udp" && kind != "cli" && kind != "ssh")
            throw std::runtime_error("Unknown protocol");

        int seconds = std::stoi(argv[2]), workers = argc > 4 ? std::stoi(argv[4]) : 32;

        if (seconds <= 0 || workers <= 0)
            throw std::runtime_error("Positive duration and worker count required");

        return Test(kind, seconds, argv[3], workers).run();
    }
    catch (const std::exception& e)
    {
        log(std::string("FAIL ") + e.what());
        return 1;
    }
}
