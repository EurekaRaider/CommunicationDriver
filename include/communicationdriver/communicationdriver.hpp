#pragma once
#include "communicationdriver.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>
#include <map>
namespace comm {
using Bytes=std::vector<std::byte>;
using Duration=std::chrono::milliseconds;
enum class Encoding { UTF8=CD_ENCODING_UTF8,ASCII=CD_ENCODING_ASCII,GB18030=CD_ENCODING_GB18030 };
enum class ExecutionMode { Persistent=CD_PERSISTENT,OneShot=CD_ONE_SHOT };
enum class ShellKind { Default,Cmd,WindowsPowerShell,Pwsh,Sh,Bash,Zsh };
enum class State { Created,Opening,Open,Closing,Closed,Faulted,NeedsRecovery };
enum class Stream { StandardOutput,StandardError };
enum class CompletionReason { None,CommandCompleted,ProtocolBoundary,Idle,Matched };
enum class BatchItemState { Completed,Failed,TimedOut,Cancelled,NotStarted };
enum class Parity { None,Odd,Even };enum class StopBits { One,OnePointFive,Two };enum class FlowControl { None,Software,Hardware };
struct ErrorInfo {cd_status_t code=CD_OK;std::string message,backend,partial_text,partial_file;int64_t native_code=0;Bytes partial_bytes;std::optional<uint64_t> confirmed_progress;};
class CommunicationError:public std::runtime_error {public:ErrorInfo info;explicit CommunicationError(ErrorInfo e):std::runtime_error(e.message),info(std::move(e)){} };
namespace detail {
inline cd_string_view_t view(std::string_view s){return {s.data(),s.size()};}
inline cd_bytes_view_t view(std::span<const std::byte> b){return {reinterpret_cast<const uint8_t*>(b.data()),b.size()};}
inline std::string string(cd_string_view_t s){return s.size?std::string(s.data,s.size):"";}
inline Bytes bytes(cd_bytes_view_t b){if(!b.size)return {};auto begin=reinterpret_cast<const std::byte*>(b.data);return {begin,begin+b.size};}
template<class T> T sized(){T t{};t.struct_size=sizeof(T);return t;}
inline ErrorInfo error(cd_result_t* r,cd_status_t status=CD_OK){auto e=sized<cd_error_info_t>();cd_result_get_error(r,&e);return {r?e.code:status,r?string(e.message):cd_status_name(status),string(e.backend),string(e.partial_text),string(e.partial_file),e.native_code,bytes(e.partial_bytes),e.has_confirmed_progress?std::optional<uint64_t>(e.confirmed_progress):std::nullopt};}
inline void checked(cd_status_t s,cd_result_t* r=nullptr){if(s!=CD_OK)throw CommunicationError(error(r,s));}
struct ResultHandle {cd_result_t* h=nullptr;explicit ResultHandle(cd_result_t* r):h(r){}~ResultHandle(){cd_result_release(h);}ResultHandle(const ResultHandle&)=delete;};
}
class Check {
 int kind_=0;bool text_=true;std::string text_value_;Bytes byte_value_;
 Check(int kind,std::string_view s):kind_(kind),text_value_(s){}
 Check(int kind,std::span<const std::byte> b):kind_(kind),text_(false),byte_value_(b.begin(),b.end()){}
public:
 static Check contains(std::string_view s){return {CD_CHECK_CONTAINS,s};}static Check contains(std::span<const std::byte> b){return {CD_CHECK_CONTAINS,b};}
 static Check equals(std::string_view s){return {CD_CHECK_EQUALS,s};}static Check equals(std::span<const std::byte> b){return {CD_CHECK_EQUALS,b};}
 static Check regex(std::string_view s){return {CD_CHECK_REGEX,s};}
 cd_check_t native()const {return {sizeof(cd_check_t),kind_,text_,0,detail::view(text_value_),detail::view(byte_value_)};}
};
class ReceiveUntil {
 cd_receive_until_t rule_{};std::string text_;Bytes bytes_;
 explicit ReceiveUntil(cd_receive_until_t rule):rule_(rule){}
public:
 ReceiveUntil(){rule_.struct_size=sizeof(rule_);}
 static ReceiveUntil bytes(uint64_t count){return ReceiveUntil(cd_receive_until_bytes(count));}
 static ReceiveUntil characters(uint64_t count){return ReceiveUntil(cd_receive_until_characters(count));}
 static ReceiveUntil delimiter(std::string_view s,bool include=false){ReceiveUntil r(cd_receive_until_delimiter_string({},include));r.text_=s;return r;}
 static ReceiveUntil delimiter(std::span<const std::byte> b,bool include=false){ReceiveUntil r(cd_receive_until_delimiter_bytes({},include));r.bytes_={b.begin(),b.end()};return r;}
 static ReceiveUntil line(std::string_view s="\n"){return delimiter(s);}
 static ReceiveUntil pattern(std::string_view s){ReceiveUntil r(cd_receive_until_pattern({}));r.text_=s;return r;}
 static ReceiveUntil idle(Duration d){return ReceiveUntil(cd_receive_until_idle(static_cast<uint64_t>(d.count())));}
 static ReceiveUntil message(){return ReceiveUntil(cd_receive_until_message());}static ReceiveUntil available(){return ReceiveUntil(cd_receive_until_available());}
 cd_receive_until_t native()const {auto r=rule_;r.text=detail::view(text_);r.bytes=detail::view(bytes_);return r;}
};
struct ConnectionOptions {
 Encoding encoding=Encoding::UTF8;std::string new_line="\n";std::optional<ReceiveUntil> receive_until;
 Duration connect_timeout{10000},io_timeout{5000},execution_timeout{30000},close_timeout{10000};uint64_t max_response_bytes=1048576;
 cd_connection_options_t native()const {cd_connection_options_t o;cd_connection_options_init(&o);o.encoding=static_cast<int>(encoding);o.new_line=detail::view(new_line);if(receive_until)o.receive_until=receive_until->native();o.connect_timeout_ms=connect_timeout.count();o.io_timeout_ms=io_timeout.count();o.execution_timeout_ms=execution_timeout.count();o.close_timeout_ms=close_timeout.count();o.max_response_bytes=max_response_bytes;return o;}
};
struct TcpOptions:ConnectionOptions{std::string host;uint16_t port=0;};
struct UdpOptions:ConnectionOptions{std::string remote_host,local_host;uint16_t remote_port=0,local_port=0;};
struct SerialOptions:ConnectionOptions{std::string port_name;uint32_t baud_rate=115200,data_bits=8;Parity parity=Parity::None;StopBits stop_bits=StopBits::One;FlowControl flow_control=FlowControl::None;};
struct SshAuth {int kind=0;std::string secret,passphrase;static SshAuth password(std::string s){return {CD_AUTH_PASSWORD,std::move(s),{}};}static SshAuth private_key(std::string path,std::string phrase={}){return {CD_AUTH_PRIVATE_KEY,std::move(path),std::move(phrase)};}};
struct HostKey {int kind=0;std::string value;static HostKey known_hosts(std::string path){return {CD_HOST_KEY_KNOWN_HOSTS,std::move(path)};}static HostKey sha256(std::string fingerprint){return {CD_HOST_KEY_SHA256,std::move(fingerprint)};}};
struct SshOptions:ConnectionOptions{std::string host,user_name,session_command;uint16_t port=22;SshAuth auth;HostKey host_key;ShellKind shell=ShellKind::Default;ExecutionMode execution_mode=ExecutionMode::Persistent;};
struct AdbOptions:ConnectionOptions{std::string device_serial,executable_path,server_host="127.0.0.1",session_command;uint16_t server_port=5037;ExecutionMode execution_mode=ExecutionMode::Persistent;};
struct CliOptions:ConnectionOptions{ShellKind shell=ShellKind::Default;std::string executable_path,working_directory;std::vector<std::string> arguments;std::map<std::string,std::string> environment;ExecutionMode execution_mode=ExecutionMode::Persistent;};
struct CallOptions {std::optional<Duration> timeout;std::optional<Encoding> encoding;std::string new_line;uint64_t max_response_bytes=0;std::optional<ReceiveUntil> receive_until;Stream read_stream=Stream::StandardOutput,check_stream=Stream::StandardOutput;
 cd_call_options_t native()const {cd_call_options_t o;cd_call_options_init(&o);if(timeout)o.timeout_ms=timeout->count();if(encoding)o.encoding=static_cast<int>(*encoding);o.new_line=detail::view(new_line);o.max_response_bytes=max_response_bytes;if(receive_until)o.receive_until=receive_until->native();o.read_stream=static_cast<int>(read_stream);o.check_stream=static_cast<int>(check_stream);return o;}
};
struct TransferOptions {std::optional<Duration> timeout;bool overwrite=true;cd_transfer_options_t native()const {cd_transfer_options_t o;cd_transfer_options_init(&o);if(timeout)o.timeout_ms=timeout->count();o.overwrite=overwrite;return o;}};
struct CheckResult {bool passed=false;std::optional<std::string> matched_text;std::optional<Bytes> matched_bytes;std::vector<std::string> groups;std::string failure_reason;};
template<class T> struct ExecutionResult {T response{},standard_error{};std::optional<CheckResult> check;std::optional<int> exit_code;CompletionReason completion_reason=CompletionReason::None;std::chrono::nanoseconds elapsed{};};
struct TransferResult {std::string source,destination;std::optional<uint64_t> bytes_transferred;std::chrono::nanoseconds elapsed{};};
namespace detail {
inline std::optional<CheckResult> check_result(cd_result_t* h){auto c=sized<cd_check_result_t>();checked(cd_result_get_check(h,&c));if(!c.has_value)return {};CheckResult r;r.passed=c.passed;if(c.matched_text.data&&c.matched_text.size)r.matched_text=string(c.matched_text);if(c.matched_bytes.data&&c.matched_bytes.size)r.matched_bytes=bytes(c.matched_bytes);for(uint64_t i=0;i<c.group_count;++i)r.groups.push_back(string(c.groups[i]));r.failure_reason=string(c.failure_reason);return r;}
template<class T> T response(cd_result_t* h,int stream){if constexpr(std::is_same_v<T,std::string>){cd_string_view_t v{};checked(cd_result_get_text(h,stream,&v));return string(v);}else {cd_bytes_view_t v{};checked(cd_result_get_bytes(h,stream,&v));return bytes(v);}}
template<class T> T convert(cd_result_t* h){checked(cd_result_get_status(h),h);if constexpr(std::is_same_v<T,void>)return;else if constexpr(std::is_same_v<T,std::string>||std::is_same_v<T,Bytes>)return response<T>(h,0);else if constexpr(std::is_same_v<T,TransferResult>){auto i=sized<cd_transfer_result_t>();checked(cd_result_get_transfer(h,&i));return {string(i.source),string(i.destination),i.has_bytes_transferred?std::optional<uint64_t>(i.bytes_transferred):std::nullopt,std::chrono::nanoseconds(i.elapsed_ns)};}else {using R=decltype(T::response);auto info=sized<cd_execution_info_t>();checked(cd_result_get_execution_info(h,&info));return {response<R>(h,0),response<R>(h,1),check_result(h),info.has_exit_code?std::optional<int>(info.exit_code):std::nullopt,static_cast<CompletionReason>(info.completion_reason),std::chrono::nanoseconds(info.elapsed_ns)};}}
struct OperationControl {cd_operation_t* handle;std::atomic<bool> consumed{false};std::unique_ptr<std::stop_callback<std::function<void()>>> cancellation;
 explicit OperationControl(cd_operation_t* h):handle(h){}~OperationControl(){cancellation.reset();cd_operation_release(handle);}};
}
template<class T> class Operation {
 std::shared_ptr<detail::OperationControl> control_;
public:
 explicit Operation(cd_operation_t* h,std::stop_token stop={}):control_(std::make_shared<detail::OperationControl>(h)){if(stop.stop_possible())control_->cancellation=std::make_unique<std::stop_callback<std::function<void()>>>(stop,[h]{cd_operation_cancel(h);});}
 Operation(Operation&&)=default;Operation& operator=(Operation&&)=default;Operation(const Operation&)=delete;
 bool is_completed()const {return cd_operation_is_completed(control_->handle)!=0;}
 void wait()const {cd_result_t* h=nullptr;detail::checked(cd_operation_wait(control_->handle,CD_INFINITE,&h));cd_result_release(h);}
 bool wait_for(Duration timeout)const {cd_result_t* h=nullptr;auto s=cd_operation_wait(control_->handle,timeout.count(),&h);cd_result_release(h);if(s==CD_TIMEOUT)return false;detail::checked(s);return true;}
 T get(){if(control_->consumed.exchange(true))throw std::logic_error("Operation result already consumed");cd_result_t* h=nullptr;detail::checked(cd_operation_wait(control_->handle,CD_INFINITE,&h));detail::ResultHandle owner(h);return detail::convert<T>(h);}
 void cancel()const {cd_operation_cancel(control_->handle);}
};
class Driver;
class Connection {
 std::shared_ptr<cd_connection_t> handle_;std::shared_ptr<cd_driver_t> driver_;
 friend class Driver;friend struct BatchItem;friend class Batch;
 Connection(cd_connection_t* h,std::shared_ptr<cd_driver_t> d):handle_(h,cd_connection_release),driver_(std::move(d)){}
 template<class T,class F> Operation<T> begin(F&& call,std::stop_token stop)const {cd_operation_t* op=nullptr;detail::checked(call(handle_.get(),&op));return Operation<T>(op,stop);}
public:
 State state()const {return static_cast<State>(cd_connection_get_state(handle_.get()));}
 cd_capabilities_t capabilities()const {return cd_connection_get_capabilities(handle_.get());}
 Operation<void> open_async(const CallOptions& options={}, std::stop_token stop={})const {auto o=options.native();return begin<void>([&](auto* c,auto** op){return cd_open_async(c,&o,nullptr,nullptr,op);},stop);}
 void open(const CallOptions& options={}, std::stop_token stop={})const {return open_async(options,stop).get();}
 Operation<void> close_async(const CallOptions& options={}, std::stop_token stop={})const {auto o=options.native();return begin<void>([&](auto* c,auto** op){return cd_close_async(c,&o,nullptr,nullptr,op);},stop);}
 void close(const CallOptions& options={}, std::stop_token stop={})const {return close_async(options,stop).get();}
 Operation<void> close_input_async(const CallOptions& options={}, std::stop_token stop={})const {auto o=options.native();return begin<void>([&](auto* c,auto** op){return cd_close_input_async(c,&o,nullptr,nullptr,op);},stop);}
 void close_input(const CallOptions& options={}, std::stop_token stop={})const {return close_input_async(options,stop).get();}
 Operation<Bytes> read_bytes_async(const CallOptions& options={}, std::stop_token stop={})const {auto o=options.native();return begin<Bytes>([&](auto* c,auto** op){return cd_read_bytes_async(c,&o,nullptr,nullptr,op);},stop);}
 Bytes read_bytes(const CallOptions& options={}, std::stop_token stop={})const {return read_bytes_async(options,stop).get();}
 Operation<std::string> read_string_async(const CallOptions& options={}, std::stop_token stop={})const {auto o=options.native();return begin<std::string>([&](auto* c,auto** op){return cd_read_string_async(c,&o,nullptr,nullptr,op);},stop);}
 std::string read_string(const CallOptions& options={}, std::stop_token stop={})const {return read_string_async(options,stop).get();}
 Operation<std::string> read_line_async(const CallOptions& options={}, std::stop_token stop={})const {auto o=options.native();return begin<std::string>([&](auto* c,auto** op){return cd_read_line_async(c,&o,nullptr,nullptr,op);},stop);}
 std::string read_line(const CallOptions& options={}, std::stop_token stop={})const {return read_line_async(options,stop).get();}
 Operation<Bytes> read_exactly_bytes_async(uint64_t count, const CallOptions& options={}, std::stop_token stop={})const {auto o=options.native();return begin<Bytes>([&](auto* c,auto** op){return cd_read_exactly_bytes_async(c,count,&o,nullptr,nullptr,op);},stop);}
 Bytes read_exactly_bytes(uint64_t count, const CallOptions& options={}, std::stop_token stop={})const {return read_exactly_bytes_async(count, options,stop).get();}
 Operation<std::string> read_exactly_string_async(uint64_t count, const CallOptions& options={}, std::stop_token stop={})const {auto o=options.native();return begin<std::string>([&](auto* c,auto** op){return cd_read_exactly_string_async(c,count,&o,nullptr,nullptr,op);},stop);}
 std::string read_exactly_string(uint64_t count, const CallOptions& options={}, std::stop_token stop={})const {return read_exactly_string_async(count, options,stop).get();}
 Operation<Bytes> read_until_bytes_async(const ReceiveUntil& until,const CallOptions& options={},std::stop_token stop={})const {auto o=options.native();auto u=until.native();return begin<Bytes>([&](auto* c,auto** op){return cd_read_until_bytes_async(c,&u,&o,nullptr,nullptr,op);},stop);}
 Bytes read_until_bytes(const ReceiveUntil& until,const CallOptions& options={},std::stop_token stop={})const {return read_until_bytes_async(until,options,stop).get();}
 Operation<std::string> read_until_string_async(const ReceiveUntil& until,const CallOptions& options={},std::stop_token stop={})const {auto o=options.native();auto u=until.native();return begin<std::string>([&](auto* c,auto** op){return cd_read_until_string_async(c,&u,&o,nullptr,nullptr,op);},stop);}
 std::string read_until_string(const ReceiveUntil& until,const CallOptions& options={},std::stop_token stop={})const {return read_until_string_async(until,options,stop).get();}
 Operation<void> write_bytes_async(std::span<const std::byte> input, const CallOptions& options={}, std::stop_token stop={})const {auto o=options.native();return begin<void>([&](auto* c,auto** op){return cd_write_bytes_async(c,detail::view(input),&o,nullptr,nullptr,op);},stop);}
 void write_bytes(std::span<const std::byte> input, const CallOptions& options={}, std::stop_token stop={})const {return write_bytes_async(input, options,stop).get();}
 Operation<void> write_string_async(std::string_view input, const CallOptions& options={}, std::stop_token stop={})const {auto o=options.native();return begin<void>([&](auto* c,auto** op){return cd_write_string_async(c,detail::view(input),&o,nullptr,nullptr,op);},stop);}
 void write_string(std::string_view input, const CallOptions& options={}, std::stop_token stop={})const {return write_string_async(input, options,stop).get();}
 Operation<void> write_line_async(std::string_view input, const CallOptions& options={}, std::stop_token stop={})const {auto o=options.native();return begin<void>([&](auto* c,auto** op){return cd_write_line_async(c,detail::view(input),&o,nullptr,nullptr,op);},stop);}
 void write_line(std::string_view input, const CallOptions& options={}, std::stop_token stop={})const {return write_line_async(input, options,stop).get();}
 Operation<ExecutionResult<std::string>> execute_async(std::string_view command, const std::optional<Check>& check={}, const CallOptions& options={}, std::stop_token stop={})const {auto o=options.native();auto ch=check?check->native():cd_check_t{};return begin<ExecutionResult<std::string>>([&](auto* c,auto** op){return cd_execute_string_async(c,detail::view(command),check?&ch:nullptr,&o,nullptr,nullptr,op);},stop);}
 ExecutionResult<std::string> execute(std::string_view command, const std::optional<Check>& check={}, const CallOptions& options={}, std::stop_token stop={})const {return execute_async(command, check, options,stop).get();}
 Operation<ExecutionResult<std::string>> execute_until_async(std::string_view command,const Check& match,const CallOptions& options={},std::stop_token stop={})const {auto o=options.native();auto ch=match.native();return begin<ExecutionResult<std::string>>([&](auto* c,auto** op){return cd_execute_until_string_async(c,detail::view(command),&ch,&o,nullptr,nullptr,op);},stop);}
 ExecutionResult<std::string> execute_until(std::string_view command,const Check& match,const CallOptions& options={},std::stop_token stop={})const {return execute_until_async(command,match,options,stop).get();}
 Operation<ExecutionResult<Bytes>> execute_async(std::span<const std::byte> command, const std::optional<Check>& check={}, const CallOptions& options={}, std::stop_token stop={})const {auto o=options.native();auto ch=check?check->native():cd_check_t{};return begin<ExecutionResult<Bytes>>([&](auto* c,auto** op){return cd_execute_bytes_async(c,detail::view(command),check?&ch:nullptr,&o,nullptr,nullptr,op);},stop);}
 ExecutionResult<Bytes> execute(std::span<const std::byte> command, const std::optional<Check>& check={}, const CallOptions& options={}, std::stop_token stop={})const {return execute_async(command, check, options,stop).get();}
 Operation<ExecutionResult<Bytes>> execute_until_async(std::span<const std::byte> command,const Check& match,const CallOptions& options={},std::stop_token stop={})const {auto o=options.native();auto ch=match.native();return begin<ExecutionResult<Bytes>>([&](auto* c,auto** op){return cd_execute_until_bytes_async(c,detail::view(command),&ch,&o,nullptr,nullptr,op);},stop);}
 ExecutionResult<Bytes> execute_until(std::span<const std::byte> command,const Check& match,const CallOptions& options={},std::stop_token stop={})const {return execute_until_async(command,match,options,stop).get();}
 Operation<TransferResult> upload_async(std::string_view source,std::string_view destination,const TransferOptions& options={},std::stop_token stop={})const {auto o=options.native();return begin<TransferResult>([&](auto* c,auto** op){return cd_upload_async(c,detail::view(source),detail::view(destination),&o,nullptr,nullptr,op);},stop);}
 TransferResult upload(std::string_view source,std::string_view destination,const TransferOptions& options={},std::stop_token stop={})const {return upload_async(source,destination,options,stop).get();}
 Operation<TransferResult> download_async(std::string_view source,std::string_view destination,const TransferOptions& options={},std::stop_token stop={})const {auto o=options.native();return begin<TransferResult>([&](auto* c,auto** op){return cd_download_async(c,detail::view(source),detail::view(destination),&o,nullptr,nullptr,op);},stop);}
 TransferResult download(std::string_view source,std::string_view destination,const TransferOptions& options={},std::stop_token stop={})const {return download_async(source,destination,options,stop).get();}
};
class Driver {
 std::shared_ptr<cd_driver_t> handle_;
 template<class F> Connection create(F&& f){cd_connection_t* c=nullptr;cd_result_t* e=nullptr;auto status=f(handle_.get(),&c,&e);detail::ResultHandle error(e);detail::checked(status,e);return Connection(c,handle_);}
public:
 Driver(){cd_driver_t* h=nullptr;detail::checked(cd_driver_create(&h));handle_={h,cd_driver_release};}
 Connection create_tcp(const TcpOptions& p){cd_tcp_options_t o;cd_tcp_options_init(&o);o.defaults=p.native();o.host=detail::view(p.host);o.port=p.port;return create([&](auto* d,auto** c,auto** e){return cd_create_tcp(d,&o,c,e);});}
 Connection create_udp(const UdpOptions& p){cd_udp_options_t o;cd_udp_options_init(&o);o.defaults=p.native();o.remote_host=detail::view(p.remote_host);o.local_host=detail::view(p.local_host);o.remote_port=p.remote_port;o.local_port=p.local_port;return create([&](auto* d,auto** c,auto** e){return cd_create_udp(d,&o,c,e);});}
 Connection create_serial(const SerialOptions& p){cd_serial_options_t o;cd_serial_options_init(&o);o.defaults=p.native();o.port_name=detail::view(p.port_name);o.baud_rate=p.baud_rate;o.data_bits=p.data_bits;o.parity=static_cast<int>(p.parity);o.stop_bits=static_cast<int>(p.stop_bits);o.flow_control=static_cast<int>(p.flow_control);return create([&](auto* d,auto** c,auto** e){return cd_create_serial(d,&o,c,e);});}
 Connection create_ssh(const SshOptions& p){cd_ssh_options_t o;cd_ssh_options_init(&o);o.defaults=p.native();o.host=detail::view(p.host);o.port=p.port;o.user_name=detail::view(p.user_name);o.auth=p.auth.kind;if(p.auth.kind==CD_AUTH_PASSWORD)o.password=detail::view(p.auth.secret);else o.private_key_path=detail::view(p.auth.secret);o.passphrase=detail::view(p.auth.passphrase);o.host_key_kind=p.host_key.kind;o.host_key=detail::view(p.host_key.value);o.session_command=detail::view(p.session_command);o.shell=static_cast<int>(p.shell);o.execution_mode=static_cast<int>(p.execution_mode);return create([&](auto* d,auto** c,auto** e){return cd_create_ssh(d,&o,c,e);});}
 Connection create_adb(const AdbOptions& p){cd_adb_options_t o;cd_adb_options_init(&o);o.defaults=p.native();o.device_serial=detail::view(p.device_serial);o.executable_path=detail::view(p.executable_path);o.server_host=detail::view(p.server_host);o.server_port=p.server_port;o.session_command=detail::view(p.session_command);o.execution_mode=static_cast<int>(p.execution_mode);return create([&](auto* d,auto** c,auto** e){return cd_create_adb(d,&o,c,e);});}
 Connection create_cli(const CliOptions& p){cd_cli_options_t o;cd_cli_options_init(&o);o.defaults=p.native();o.shell=static_cast<int>(p.shell);o.execution_mode=static_cast<int>(p.execution_mode);o.executable_path=detail::view(p.executable_path);o.working_directory=detail::view(p.working_directory);std::vector<cd_string_view_t> args;for(auto& s:p.arguments)args.push_back(detail::view(s));o.arguments=args.data();o.argument_count=args.size();std::vector<cd_environment_entry_t> env;for(auto& [k,v]:p.environment)env.push_back({detail::view(k),detail::view(v)});o.environment=env.data();o.environment_count=env.size();return create([&](auto* d,auto** c,auto** e){return cd_create_cli(d,&o,c,e);});}
 Operation<void> close_async(){cd_operation_t* op=nullptr;detail::checked(cd_driver_close_async(handle_.get(),nullptr,nullptr,&op));return Operation<void>(op);}
 void close(){close_async().get();}
};
class ResponseChecker {public:static bool is_match(std::string_view response,const Check& check){auto c=check.native();cd_result_t* r=nullptr;auto status=cd_response_checker_is_match_string(detail::view(response),&c,&r);detail::ResultHandle owner(r);detail::checked(status,r);return detail::check_result(r)->passed;}static bool is_match(std::span<const std::byte> response,const Check& check){auto c=check.native();cd_result_t* r=nullptr;auto status=cd_response_checker_is_match_bytes(detail::view(response),&c,&r);detail::ResultHandle owner(r);detail::checked(status,r);return detail::check_result(r)->passed;}};
struct BatchOptions {uint32_t max_concurrency=32;};
struct BatchItemResult {std::string key;BatchItemState state;bool succeeded=false;std::optional<ExecutionResult<std::string>> text_execution;std::optional<ExecutionResult<Bytes>> byte_execution;std::optional<TransferResult> transfer;std::optional<ErrorInfo> error;std::chrono::nanoseconds elapsed{};};
struct BatchResult {std::vector<BatchItemResult> items;bool all_succeeded=false,was_cancelled=false;std::chrono::nanoseconds elapsed{};};
struct BatchItem {
 std::string key;Connection connection;int kind;std::string command,source,destination;Bytes byte_command;std::optional<Check> check;CallOptions options;TransferOptions transfer_options;
 static BatchItem execute(std::string key,Connection c,std::string_view command,std::optional<Check> check={},CallOptions options={}){return {std::move(key),std::move(c),CD_BATCH_EXECUTE_STRING,std::string(command),{},{},{},std::move(check),std::move(options),{}};}
 static BatchItem execute(std::string key,Connection c,std::span<const std::byte> command,std::optional<Check> check={},CallOptions options={}){return {std::move(key),std::move(c),CD_BATCH_EXECUTE_BYTES,{},{},{},{command.begin(),command.end()},std::move(check),std::move(options),{}};}
 static BatchItem execute_until(std::string key,Connection c,std::string_view command,Check match,CallOptions options={}){auto item=execute(std::move(key),std::move(c),command,std::move(match),std::move(options));item.kind=CD_BATCH_EXECUTE_UNTIL_STRING;return item;}
 static BatchItem execute_until(std::string key,Connection c,std::span<const std::byte> command,Check match,CallOptions options={}){auto item=execute(std::move(key),std::move(c),command,std::move(match),std::move(options));item.kind=CD_BATCH_EXECUTE_UNTIL_BYTES;return item;}
 static BatchItem upload(std::string key,Connection c,std::string source,std::string destination,TransferOptions options={}){return {std::move(key),std::move(c),CD_BATCH_UPLOAD,{},std::move(source),std::move(destination),{},{},{},std::move(options)};}
 static BatchItem download(std::string key,Connection c,std::string source,std::string destination,TransferOptions options={}){auto item=upload(std::move(key),std::move(c),std::move(source),std::move(destination),std::move(options));item.kind=CD_BATCH_DOWNLOAD;return item;}
};
namespace detail {
template<> inline BatchResult convert<BatchResult>(cd_result_t* h){checked(cd_result_get_status(h),h);auto info=sized<cd_batch_info_t>();checked(cd_result_get_batch_info(h,&info));BatchResult result;result.all_succeeded=info.all_succeeded;result.was_cancelled=info.was_cancelled;result.elapsed=std::chrono::nanoseconds(info.elapsed_ns);for(uint64_t index=0;index<info.item_count;++index){auto i=sized<cd_batch_item_result_t>();checked(cd_result_get_batch_item(h,index,&i));BatchItemResult item;item.key=string(i.key);item.state=static_cast<BatchItemState>(i.state);item.succeeded=i.succeeded;item.elapsed=std::chrono::nanoseconds(i.elapsed_ns);if(i.result){if(cd_result_get_status(i.result)!=CD_OK)item.error=error(i.result);else {auto kind=cd_result_get_kind(i.result);if(kind==CD_RESULT_EXECUTION_TEXT)item.text_execution=convert<ExecutionResult<std::string>>(i.result);else if(kind==CD_RESULT_EXECUTION_BYTES)item.byte_execution=convert<ExecutionResult<Bytes>>(i.result);else if(kind==CD_RESULT_TRANSFER)item.transfer=convert<TransferResult>(i.result);}}result.items.push_back(std::move(item));}return result;}
}
class Batch {public:
 static Operation<BatchResult> run_async(std::span<const BatchItem> items,const BatchOptions& options={},std::stop_token stop={}){
  if(items.empty())throw std::invalid_argument("Batch requires at least one item");
  std::vector<cd_batch_item_t> native(items.size());std::vector<cd_check_t> checks(items.size());std::vector<cd_call_options_t> calls(items.size());std::vector<cd_transfer_options_t> transfers(items.size());
  for(size_t n=0;n<items.size();++n){auto& i=items[n];auto& b=native[n];b.struct_size=sizeof(b);b.kind=i.kind;b.key=detail::view(i.key);b.connection=i.connection.handle_.get();b.command=detail::view(i.command);b.bytes=detail::view(i.byte_command);if(i.check){checks[n]=i.check->native();b.check=&checks[n];}calls[n]=i.options.native();b.options=&calls[n];b.source=detail::view(i.source);b.destination=detail::view(i.destination);transfers[n]=i.transfer_options.native();b.transfer_options=&transfers[n];}
  cd_batch_options_t o;cd_batch_options_init(&o);o.max_concurrency=options.max_concurrency;cd_operation_t* op=nullptr;detail::checked(cd_batch_run_async(items[0].connection.driver_.get(),native.data(),native.size(),&o,nullptr,nullptr,&op));return Operation<BatchResult>(op,stop);
 }
 static BatchResult run(std::span<const BatchItem> items,const BatchOptions& options={},std::stop_token stop={}){return run_async(items,options,stop).get();}
};
}
