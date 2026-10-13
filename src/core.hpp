#pragma once
#include <communicationdriver/communicationdriver.h>
#include <boost/asio.hpp>
#include <boost/process.hpp>
#include <re2/re2.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <filesystem>

namespace cd {
namespace asio=boost::asio;
namespace process=boost::process;
using Clock=std::chrono::steady_clock;
using ErrorCode=boost::system::error_code;
using Completion=std::function<void(cd_status_t,std::string,int64_t)>;
struct Fault : std::exception {
 cd_status_t code; std::string message; int64_t native;
 Fault(cd_status_t c,std::string m,int64_t n=0):code(c),message(std::move(m)),native(n){}
 const char* what() const noexcept override { return message.c_str(); }
};
std::string copy(cd_string_view_t);
std::string copy(cd_bytes_view_t);
cd_string_view_t view(const std::string&);
cd_bytes_view_t bytes_view(const std::string&);
std::string decode(const std::string&,cd_encoding_t,bool final=true);
std::string encode(const std::string&,cd_encoding_t);
size_t scalar_prefix(const std::string&,uint64_t);
uint64_t scalar_count(const std::string&);
struct Check {
 int kind=CD_CHECK_NONE; bool text=true; std::string value; std::shared_ptr<RE2> regex;
 explicit Check(const cd_check_t* =nullptr);
};
struct Rule {
 int kind=CD_RECEIVE_NONE; uint64_t count=0,idle_ms=0; bool text=false,include=false;
 std::string value; std::shared_ptr<RE2> regex;
 Rule()=default; explicit Rule(const cd_receive_until_t&);
};
struct Defaults {
 cd_encoding_t encoding=CD_ENCODING_UTF8; std::string newline="\n"; Rule rule;
 uint64_t connect_ms=10000,io_ms=5000,execution_ms=30000,close_ms=10000,max_bytes=1048576;
 explicit Defaults(const cd_connection_options_t&);
};
struct Cancellation {
 std::atomic<bool> requested{false}; std::mutex mutex;
 std::vector<std::function<void()>> listeners;
 void request(); void subscribe(std::function<void()>);
};
struct Options {
 cd_encoding_t encoding; std::string newline; Rule rule; uint64_t timeout,max_bytes;
 int read_stream=0,check_stream=0; std::shared_ptr<Cancellation> cancellation;
 Options(const Defaults&,const cd_call_options_t*,uint64_t fallback);
};
struct Config {
 int backend=0; Defaults defaults; std::string host,local_host,port_name;
 uint16_t port=0,local_port=0; uint32_t baud=115200,bits=8;
 int parity=0,stop=0,flow=0,mode=CD_PERSISTENT,shell=CD_SHELL_DEFAULT;
 std::string executable,working_directory,device,server_host,session_command;
 uint16_t server_port=5037; std::vector<std::string> arguments;
 std::map<std::string,std::string> environment;
 std::string user,password,private_key,passphrase,host_key; int auth=0,key_kind=0;
 explicit Config(const cd_connection_options_t& d):defaults(d){}
 std::string name() const;
 bool managed() const { return ((backend==3||backend==4)&&session_command.empty())||(backend==5&&executable.empty()); }
};
struct Result;
struct BatchEntry { std::string key; int state=CD_BATCH_NOT_STARTED; std::shared_ptr<Result> result; };
struct Result {
 cd_status_t status=CD_OK; int kind=CD_RESULT_VOID; std::string message,backend;
 int64_t native=0; std::string raw[2],text[2],matched; bool check_present=false,passed=false,matched_is_text=true;
 std::vector<std::string> groups; std::string failure;
 int completion=CD_COMPLETION_NONE; std::optional<int> exit_code; uint64_t elapsed_ns=0;
 std::optional<uint64_t> progress; std::string source,destination,partial_file;
 std::vector<BatchEntry> items; bool was_cancelled=false;
 bool succeeded() const { return status==CD_OK&&(!check_present||passed); }
 void evaluate(const Check&,int stream,cd_encoding_t,bool final);
};
struct IoState { asio::io_context io; };
extern thread_local IoState* current_io;
struct Connection;
struct Runtime {
 std::shared_ptr<IoState> state=std::make_shared<IoState>();
 asio::executor_work_guard<asio::io_context::executor_type> guard=asio::make_work_guard(state->io);
 std::thread worker; std::mutex registry_mutex;
 std::vector<std::weak_ptr<Connection>> connections; std::atomic<bool> closed{false};
 Runtime(); ~Runtime(); void register_connection(const std::shared_ptr<Connection>&);
 void close(std::function<void()>);
};
struct Operation : std::enable_shared_from_this<Operation> {
 std::shared_ptr<Runtime> runtime; std::mutex mutex; std::condition_variable cv;
 std::shared_ptr<Result> result; std::atomic<bool> cancelled{false};
 std::function<void(std::shared_ptr<Result>)> callback; std::function<void()> cancel_hook;
 explicit Operation(std::shared_ptr<Runtime> r):runtime(std::move(r)){}
 void complete(std::shared_ptr<Result>); void cancel(); bool completed();
};
enum class Action { Open,Close,CloseInput,Read,Write,Execute,ExecuteUntil,Upload,Download };
struct Request {
 Action action; bool text=false; std::string input,source,destination; Check check; Options options;
 bool overwrite=true; std::shared_ptr<Operation> operation; Clock::time_point started=Clock::now();
 std::shared_ptr<Result> result=std::make_shared<Result>();
 std::shared_ptr<asio::steady_timer> deadline,idle; bool sent=false,writing=false,early=false;
 std::string marker; int marker_mask=0; std::optional<int> marker_exit;
 Request(Action a,bool t,Options o):action(a),text(t),options(std::move(o)){}
};
struct Backend : std::enable_shared_from_this<Backend> {
 std::shared_ptr<Runtime> runtime; Config config;
 std::function<void(int,std::string,bool)> output;
 std::function<void(cd_status_t,std::string,int64_t)> failure;
 std::function<void(std::optional<int>)> exited;
 Backend(std::shared_ptr<Runtime> r,Config c):runtime(std::move(r)),config(std::move(c)){}
 virtual ~Backend()=default;
 virtual void open(Completion)=0;
 virtual void write(std::string,Completion)=0;
 virtual void close()=0;
 virtual void close_input(Completion)=0;
 virtual void command(std::string,Completion cb) { cb(CD_NOT_SUPPORTED,"Command channels unavailable",0); }
 virtual void transfer(bool,std::string,std::string,bool,std::function<void(uint64_t)>,Completion cb) { cb(CD_NOT_SUPPORTED,"File transfer unavailable",0); }
 virtual bool command_channels() const { return false; }
};
std::shared_ptr<Backend> make_ssh(std::shared_ptr<Runtime>,Config);
std::shared_ptr<Backend> make_backend(std::shared_ptr<Runtime>,Config);
struct Connection : std::enable_shared_from_this<Connection> {
 std::shared_ptr<Runtime> runtime; Config config; cd_capabilities_t capabilities;
 std::atomic<int> state{CD_STATE_CREATED}; std::shared_ptr<Backend> persistent,one_shot;
 std::deque<std::shared_ptr<Request>> queue; std::shared_ptr<Request> active;
 std::string pending[2]; std::deque<size_t> messages[2]; bool eof[2]={false,false};
 std::optional<int> process_exit; bool process_finished=false; uint64_t generation=0;
 Connection(std::shared_ptr<Runtime>,Config);
 ~Connection(); void submit(std::shared_ptr<Request>); void next(); void start();
 void attach(const std::shared_ptr<Backend>&,bool isolated=false);
 void receive(int,std::string,bool,bool); void process_data();
 void finish(cd_status_t=CD_OK,std::string={},int64_t=0,bool advance=true);
 void abort(const std::shared_ptr<Request>&,cd_status_t,std::string);
 void force_close(); void ensure_persistent(Completion);
 bool boundary(std::string&,const Rule&,const Options&,size_t&,size_t&);
 void consume_pending(int stream,size_t count);
 std::string framed_command(const std::string&,const std::string&);
};
std::shared_ptr<Result> error_result(const Fault&);
std::shared_ptr<Cancellation> cancellation(cd_cancel_source_t*);
}

// These objects are only visible inside the implementation; the ABI stays opaque.
template<class T> struct CdHandle {
 std::atomic<uint32_t> refs{1}; std::shared_ptr<T> value;
 explicit CdHandle(std::shared_ptr<T> v):value(std::move(v)){}
};
struct cd_driver : CdHandle<cd::Runtime> { using CdHandle::CdHandle; ~cd_driver(); };
struct cd_connection : CdHandle<cd::Connection> { using CdHandle::CdHandle; ~cd_connection(); };
struct cd_operation : CdHandle<cd::Operation> { using CdHandle::CdHandle; };
struct cd_cancel_source : CdHandle<cd::Cancellation> { using CdHandle::CdHandle; };
struct cd_result : CdHandle<cd::Result> {
 std::vector<cd_string_view_t> group_views;
 std::vector<cd_result*> item_handles;
 explicit cd_result(std::shared_ptr<cd::Result>);
 ~cd_result();
};
