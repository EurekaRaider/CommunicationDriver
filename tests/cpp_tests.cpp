#include <communicationdriver/communicationdriver.hpp>
#include <iostream>

static void expect(bool value, const char* reason)
{
    if (!value)
        throw std::runtime_error(reason);
}

int main()
{
    try
    {
        comm::Driver driver;
        comm::CliOptions options;
        auto connection = driver.create_cli(options);
        connection.open();
#ifdef _WIN32
        const char* ready = "echo|set /p=READY";
#else
        const char* ready = "printf READY";
#endif

        auto operation = connection.execute_async(ready, comm::Check::contains("READY"));
        operation.wait();
        expect(operation.is_completed(), "completed observer");
        expect(operation.get().check->passed, "get result");

        try
        {
            operation.get();
            throw std::runtime_error("double get accepted");
        }
        catch (const std::logic_error&)
        {
        }

        {
            auto abandoned = connection.execute_async(ready);
        }
        expect(connection.execute(ready).response == "READY",
            "discard does not cancel or lose transaction ownership");

        std::vector<comm::BatchItem> items;
        items.push_back(
            comm::BatchItem::execute("one", connection, ready, comm::Check::contains("READY")));
        items.push_back(
            comm::BatchItem::execute("two", connection, ready, comm::Check::contains("READY")));

        auto batch = comm::Batch::run(items);
        expect(batch.all_succeeded && batch.items[0].key == "one" && batch.items[1].key == "two",
            "batch same-connection order");
#ifndef _WIN32
        connection.write_string("printf ERR >&2\n");
        comm::CallOptions stderr_options;
        stderr_options.read_stream = comm::Stream::StandardError;
        expect(connection.read_exactly_string(3, stderr_options) == "ERR",
            "C++ stderr stream selection");
        connection.write_string("printf PART >&2\n");
        stderr_options.timeout = comm::Duration(25);
        try
        {
            connection.read_exactly_string(8, stderr_options);
            throw std::runtime_error("partial stderr read unexpectedly completed");
        }
        catch (const comm::CommunicationError& e)
        {
            expect(e.info.code == CD_TIMEOUT && e.info.partial_text == "PART",
                "stderr timeout preserves the selected stream in ErrorInfo");
        }
        std::stop_source stop;
        auto cancel = connection.execute_until_async(
            "printf PART; sleep 1", comm::Check::contains("NEVER"), {}, stop.get_token());
        stop.request_stop();
        try
        {
            cancel.get();
            throw std::runtime_error("stop token ignored");
        }
        catch (const comm::CommunicationError& e)
        {
            expect(e.info.code == CD_CANCELLED, "stop token reaches native core");
        }
#endif
        connection.close();
#ifdef _WIN32
        std::vector<comm::ShellKind> shells = {
            comm::ShellKind::Cmd, comm::ShellKind::WindowsPowerShell, comm::ShellKind::Pwsh};
#elif defined(__APPLE__)
        std::vector<comm::ShellKind> shells = {
            comm::ShellKind::Sh, comm::ShellKind::Bash, comm::ShellKind::Zsh};
#else
        std::vector<comm::ShellKind> shells = {comm::ShellKind::Sh, comm::ShellKind::Bash};
#endif

        for (auto kind : shells)
            for (auto mode : {comm::ExecutionMode::Persistent, comm::ExecutionMode::OneShot})
            {
                comm::CliOptions shell_options;
                shell_options.shell = kind;
                shell_options.execution_mode = mode;

                auto shell = driver.create_cli(shell_options);
                shell.open();

                auto command =
                    kind == comm::ShellKind::Cmd ? "echo|set /p=READY"
                    : kind == comm::ShellKind::WindowsPowerShell || kind == comm::ShellKind::Pwsh
                        ? "[Console]::Out.Write('READY')"
                        : "printf READY";
                expect(shell.execute(command).response == "READY",
                    "explicit shell kind and execution mode");
                shell.close();
            }

        driver.close();
        std::cout
            << "PASS C++ RAII, Operation one-get, observer, abandoned handle, stderr, stop_token, Batch and explicit shells\n";

        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL " << e.what() << "\n";
        return 1;
    }
}
