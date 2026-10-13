#include "core.hpp"
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <random>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#else
#include <iconv.h>
#endif

namespace cd {
thread_local IoState* current_io=nullptr;
static void require(bool condition,const char* message) { if(!condition) throw Fault(CD_INVALID_ARGUMENT,message); }
std::string copy(cd_string_view_t v) {
 require(v.size<=SIZE_MAX&&(v.data||v.size==0),"Invalid string view");
 return v.size?std::string(v.data,static_cast<size_t>(v.size)):std::string();
}
std::string copy(cd_bytes_view_t v) { return copy({reinterpret_cast<const char*>(v.data),v.size}); }
cd_string_view_t view(const std::string& s) { return {s.data(),s.size()}; }
cd_bytes_view_t bytes_view(const std::string& s) { return {reinterpret_cast<const uint8_t*>(s.data()),s.size()}; }
// Returns a valid UTF-8 prefix. Incomplete terminal scalars stay buffered until the next block.
static size_t valid_utf8(const std::string& s,bool final) {
 size_t i=0;
 while(i<s.size()) {
  unsigned char c=s[i]; size_t n=c<0x80?1:(c>=0xc2&&c<=0xdf?2:(c>=0xe0&&c<=0xef?3:(c>=0xf0&&c<=0xf4?4:0)));
  if(!n) throw Fault(CD_ENCODING_ERROR,"Invalid UTF-8 leading byte");
  if(s.size()-i<n) { if(final) throw Fault(CD_ENCODING_ERROR,"Incomplete UTF-8 scalar"); return i; }
  for(size_t j=1;j<n;++j) if((static_cast<unsigned char>(s[i+j])&0xc0)!=0x80) throw Fault(CD_ENCODING_ERROR,"Invalid UTF-8 continuation");
  if(n==3&&((c==0xe0&&static_cast<unsigned char>(s[i+1])<0xa0)||(c==0xed&&static_cast<unsigned char>(s[i+1])>=0xa0))) throw Fault(CD_ENCODING_ERROR,"Invalid UTF-8 scalar");
  if(n==4&&((c==0xf0&&static_cast<unsigned char>(s[i+1])<0x90)||(c==0xf4&&static_cast<unsigned char>(s[i+1])>=0x90))) throw Fault(CD_ENCODING_ERROR,"Invalid UTF-8 scalar");
  i+=n;
 }
 return i;
}
size_t scalar_prefix(const std::string& s,uint64_t count) {
 size_t i=0; uint64_t n=0;
 while(i<s.size()&&n<count) { unsigned char c=s[i]; i+=c<0x80?1:(c<0xe0?2:(c<0xf0?3:4)); ++n; }
 return n==count?i:std::string::npos;
}
uint64_t scalar_count(const std::string& s) { uint64_t n=0; for(unsigned char c:s) if((c&0xc0)!=0x80) ++n; return n; }
#ifndef _WIN32
static std::string convert(const std::string& input,const char* to,const char* from,bool final) {
 iconv_t converter=iconv_open(to,from);
 if(converter==reinterpret_cast<iconv_t>(-1)) throw Fault(CD_ENCODING_ERROR,"Encoding converter unavailable",errno);
 std::string result(input.size()*4+16,'\0'); char* source=const_cast<char*>(input.data()); size_t source_size=input.size(); char* target=result.data(); size_t space=result.size();
 size_t rc=iconv(converter,&source,&source_size,&target,&space); int error=errno; iconv_close(converter);
 if(rc==static_cast<size_t>(-1)&&!(error==EINVAL&&!final)) throw Fault(CD_ENCODING_ERROR,"Invalid encoding sequence",error);
 result.resize(result.size()-space); return result;
}
#endif
std::string decode(const std::string& s,cd_encoding_t encoding,bool final) {
 if(encoding==CD_ENCODING_UTF8||encoding==CD_ENCODING_DEFAULT) return s.substr(0,valid_utf8(s,final));
 if(encoding==CD_ENCODING_ASCII) { for(unsigned char c:s) if(c>127) throw Fault(CD_ENCODING_ERROR,"Non-ASCII byte"); return s; }
 if(encoding!=CD_ENCODING_GB18030) throw Fault(CD_INVALID_ARGUMENT,"Unknown encoding");
#ifdef _WIN32
 // GB18030 is stateless. Incomplete suffix is retained; invalid complete sequences are errors.
 size_t prefix=s.size();
 while(prefix>0) {
  int n=MultiByteToWideChar(54936,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(prefix),nullptr,0);
  if(n) { std::wstring w(n,0); MultiByteToWideChar(54936,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(prefix),w.data(),n); int m=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w.data(),n,nullptr,0,nullptr,nullptr); std::string out(m,0); WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w.data(),n,out.data(),m,nullptr,nullptr); return out; }
  if(final||s.size()-prefix>=3) break; --prefix;
 }
 if(s.empty()) return {}; throw Fault(CD_ENCODING_ERROR,"Invalid GB18030 sequence",GetLastError());
#else
 return convert(s,"UTF-8","GB18030",final);
#endif
}
std::string encode(const std::string& s,cd_encoding_t encoding) {
 valid_utf8(s,true);
 if(encoding==CD_ENCODING_UTF8||encoding==CD_ENCODING_DEFAULT) return s;
 if(encoding==CD_ENCODING_ASCII) return decode(s,encoding);
 if(encoding!=CD_ENCODING_GB18030) throw Fault(CD_INVALID_ARGUMENT,"Unknown encoding");
#ifdef _WIN32
 if(s.empty()) return {};
 int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0); std::wstring w(n,0);
 MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),w.data(),n);
 int m=WideCharToMultiByte(54936,WC_ERR_INVALID_CHARS,w.data(),n,nullptr,0,nullptr,nullptr);
 if(!m) throw Fault(CD_ENCODING_ERROR,"GB18030 conversion failed",GetLastError());
 std::string out(m,0); WideCharToMultiByte(54936,WC_ERR_INVALID_CHARS,w.data(),n,out.data(),m,nullptr,nullptr); return out;
#else
 return convert(s,"GB18030","UTF-8",true);
#endif
}
static std::shared_ptr<RE2> compile_regex(const std::string& pattern) {
 auto re=std::make_shared<RE2>(pattern,RE2::Quiet);
 if(!re->ok()) throw Fault(CD_INVALID_ARGUMENT,"Invalid RE2 pattern: "+re->error()); return re;
}
Check::Check(const cd_check_t* c) {
 if(!c) return; require(c->struct_size>=sizeof(*c),"Check struct_size too small");
 kind=c->kind; text=c->is_text!=0; require(kind>=CD_CHECK_NONE&&kind<=CD_CHECK_REGEX,"Invalid check kind");
 value=text?copy(c->text):copy(c->bytes); if(text) valid_utf8(value,true);
 if(kind==CD_CHECK_REGEX) { require(text,"Regex requires text"); regex=compile_regex(value); }
}
Rule::Rule(const cd_receive_until_t& r):kind(r.kind),count(r.count),idle_ms(r.idle_ms),text(r.is_text!=0),include(r.include_delimiter!=0),value(r.is_text?copy(r.text):copy(r.bytes)) {
 require(r.struct_size>=sizeof(r),"ReceiveUntil struct_size too small");
 require(kind>=CD_RECEIVE_NONE&&kind<=CD_RECEIVE_AVAILABLE,"Invalid receive kind");
 if(kind==CD_RECEIVE_BYTES||kind==CD_RECEIVE_CHARACTERS) require(count>0&&count<=SIZE_MAX,"Receive count must be positive");
 if(kind==CD_RECEIVE_DELIMITER) require(!value.empty(),"Delimiter cannot be empty");
 if(kind==CD_RECEIVE_IDLE) require(idle_ms>0&&idle_ms<=INT64_MAX,"Idle must fit a positive duration");
 if(text) valid_utf8(value,true); if(kind==CD_RECEIVE_PATTERN) regex=compile_regex(value);
}
Defaults::Defaults(const cd_connection_options_t& d) {
 require(d.struct_size>=sizeof(d),"Defaults struct_size too small");
 encoding=d.encoding==CD_ENCODING_DEFAULT?CD_ENCODING_UTF8:d.encoding;
 require(encoding>=CD_ENCODING_UTF8&&encoding<=CD_ENCODING_GB18030,"Invalid encoding");
 newline=copy(d.new_line); require(!newline.empty(),"NewLine cannot be empty"); valid_utf8(newline,true);
 rule=Rule(d.receive_until); connect_ms=d.connect_timeout_ms;io_ms=d.io_timeout_ms;execution_ms=d.execution_timeout_ms;close_ms=d.close_timeout_ms;max_bytes=d.max_response_bytes;
 require(connect_ms&&io_ms&&execution_ms&&close_ms&&max_bytes,"Timeouts and buffer limit must be positive");
 require(connect_ms<=INT64_MAX&&io_ms<=INT64_MAX&&execution_ms<=INT64_MAX&&close_ms<=INT64_MAX&&max_bytes<=SIZE_MAX,"Timeout or buffer limit is out of range");
}
Options::Options(const Defaults& d,const cd_call_options_t* o,uint64_t fallback):encoding(d.encoding),newline(d.newline),rule(d.rule),timeout(fallback),max_bytes(d.max_bytes) {
 if(!o) return; require(o->struct_size>=sizeof(*o),"CallOptions struct_size too small");
 if(o->encoding!=CD_ENCODING_DEFAULT) encoding=o->encoding;
 require(encoding>=CD_ENCODING_UTF8&&encoding<=CD_ENCODING_GB18030,"Invalid encoding");
 if(o->timeout_ms) timeout=o->timeout_ms; if(o->max_response_bytes) max_bytes=o->max_response_bytes;
 require(timeout<=INT64_MAX&&max_bytes<=SIZE_MAX,"Timeout or buffer limit is out of range");
 if(o->new_line.size) newline=copy(o->new_line); valid_utf8(newline,true);
 if(o->receive_until.kind!=CD_RECEIVE_NONE) rule=Rule(o->receive_until);
 read_stream=o->read_stream;check_stream=o->check_stream;
 require(read_stream>=0&&read_stream<=1&&check_stream>=0&&check_stream<=1,"Invalid stream");
 cancellation=cd::cancellation(o->cancellation);
}
void Cancellation::request() {if(requested.exchange(true))return;std::vector<std::weak_ptr<Operation>> targets;{std::lock_guard lock(mutex);targets.swap(listeners);}for(auto& weak:targets)if(auto op=weak.lock())op->cancel();}
void Cancellation::subscribe(std::weak_ptr<Operation> weak) {{std::lock_guard lock(mutex);if(!requested){std::erase_if(listeners,[](auto& entry){return entry.expired();});listeners.push_back(std::move(weak));return;}}if(auto op=weak.lock())op->cancel();}
std::string Config::name() const { static const char* names[]={"tcp","udp","serial","ssh","adb","cli"}; return names[backend]; }
void Result::evaluate(const Check& c,int stream,cd_encoding_t encoding,bool final) {
 if(c.kind==CD_CHECK_NONE) return;
 check_present=true;matched_is_text=c.text; matched.clear();groups.clear();failure.clear();
 const std::string data=c.text?decode(raw[stream],encoding,final):raw[stream]; size_t pos=std::string::npos;
 if(c.kind==CD_CHECK_CONTAINS) {pos=data.find(c.value);passed=pos!=std::string::npos;if(passed) matched=c.value;}
 else if(c.kind==CD_CHECK_EQUALS) {passed=data==c.value;if(passed) matched=data;}
 else {std::vector<absl::string_view> captures(static_cast<size_t>(c.regex->NumberOfCapturingGroups())+1); passed=c.regex->Match(data,0,data.size(),RE2::UNANCHORED,captures.data(),static_cast<int>(captures.size())); if(passed) {matched=std::string(captures[0]);for(size_t i=1;i<captures.size();++i) groups.emplace_back(captures[i]);}}
 if(!passed) failure="Response did not match";
}
Runtime::Runtime() { auto s=state; worker=std::thread([s]{current_io=s.get();s->io.run();current_io=nullptr;}); }
Runtime::~Runtime() { guard.reset(); if(worker.joinable()) {if(worker.get_id()==std::this_thread::get_id()) worker.detach();else worker.join();} }
void Runtime::register_connection(const std::shared_ptr<Connection>& c) { std::lock_guard lock(registry_mutex);if(closed) throw Fault(CD_CLOSED,"Driver is closed");connections.push_back(c); }
void Runtime::close(std::function<void()> callback) {
 closed=true; std::vector<std::shared_ptr<Connection>> list;
 {std::lock_guard lock(registry_mutex);for(auto& w:connections) if(auto c=w.lock()) list.push_back(std::move(c));}
 auto self=state;asio::post(state->io,[self,list=std::move(list),cb=std::move(callback)]() mutable {for(auto& c:list) c->force_close();if(self->file_jobs)self->file_waiters.push_back(std::move(cb));else asio::post(self->io,std::move(cb));});
}
void Runtime::file_job(std::function<void()> work,std::function<void(std::exception_ptr)> done) {
 auto s=state;++s->file_jobs;asio::post(s->files,[s,work=std::move(work),done=std::move(done)]() mutable {
  std::exception_ptr error;try{work();}catch(...){error=std::current_exception();}
  work={};
  asio::post(s->io,[s,error,done=std::move(done)]() mutable {done(error);if(--s->file_jobs==0){auto waiters=std::exchange(s->file_waiters,{});for(auto& cb:waiters)asio::post(s->io,std::move(cb));}});
 });
}
void Runtime::after_files(std::function<void()> done) {if(state->file_jobs)state->file_waiters.push_back(std::move(done));else asio::post(state->io,std::move(done));}
void Operation::complete(std::shared_ptr<Result> r) {
 std::function<void(std::shared_ptr<Result>)> cb;
 {std::lock_guard lock(mutex);if(result) return;result=r;cb=std::move(callback);cancel_hook={};} cv.notify_all(); if(cb) cb(std::move(r));
}
bool Operation::completed() {std::lock_guard lock(mutex);return bool(result);}
void Operation::cancel() {
 if(cancelled.exchange(true)) return; std::function<void()> f; {std::lock_guard lock(mutex);f=cancel_hook;} if(f) f();
}
std::shared_ptr<Result> error_result(const Fault& f) {auto r=std::make_shared<Result>();r->status=f.code;r->message=f.message;r->native=f.native;return r;}

struct StreamBackend : Backend {
 asio::ip::tcp::socket tcp; asio::ip::udp::socket udp; asio::serial_port serial;
 asio::ip::tcp::resolver tcp_resolver; asio::ip::udp::resolver udp_resolver;
 std::array<char,65536> buffer{}; bool stopped=false;
 StreamBackend(std::shared_ptr<Runtime> r,Config c):Backend(r,std::move(c)),tcp(r->state->io),udp(r->state->io),serial(r->state->io),tcp_resolver(r->state->io),udp_resolver(r->state->io){}
 void open(Completion cb) override {
  auto self=std::static_pointer_cast<StreamBackend>(shared_from_this());
  if(config.backend==0) tcp_resolver.async_resolve(config.host,std::to_string(config.port),[self,cb](ErrorCode ec,asio::ip::tcp::resolver::results_type endpoints){
   if(self->stopped)return;
   if(ec) {cb(CD_CONNECTION_FAILED,ec.message(),ec.value());return;}
   asio::async_connect(self->tcp,endpoints,[self,cb](ErrorCode e,const auto&){if(e) cb(CD_CONNECTION_FAILED,e.message(),e.value());else {self->tcp.set_option(asio::ip::tcp::no_delay(true));cb(CD_OK,{},0);self->pump();}});
  });
  else if(config.backend==1) udp_resolver.async_resolve(config.host,std::to_string(config.port),[self,cb](ErrorCode ec,asio::ip::udp::resolver::results_type eps){
   if(self->stopped)return;
   if(ec) {cb(CD_CONNECTION_FAILED,ec.message(),ec.value());return;}
   ErrorCode e;auto endpoint=eps.begin()->endpoint();self->udp.open(endpoint.protocol(),e);
   if(!e) {auto address=self->config.local_host.empty()?(endpoint.address().is_v4()?asio::ip::address(asio::ip::address_v4::any()):asio::ip::address(asio::ip::address_v6::any())):asio::ip::make_address(self->config.local_host,e);if(!e) self->udp.bind({address,self->config.local_port},e);}
   if(!e) self->udp.connect(endpoint,e);if(e) cb(CD_CONNECTION_FAILED,e.message(),e.value());else {cb(CD_OK,{},0);self->pump();}
  });
  else {
   ErrorCode e;serial.open(config.port_name,e);
   if(!e) serial.set_option(asio::serial_port_base::baud_rate(config.baud),e);
   if(!e) serial.set_option(asio::serial_port_base::character_size(config.bits),e);
   if(!e) serial.set_option(asio::serial_port_base::parity(static_cast<asio::serial_port_base::parity::type>(config.parity)),e);
   if(!e) serial.set_option(asio::serial_port_base::stop_bits(static_cast<asio::serial_port_base::stop_bits::type>(config.stop)),e);
   if(!e) serial.set_option(asio::serial_port_base::flow_control(static_cast<asio::serial_port_base::flow_control::type>(config.flow)),e);
   if(e) cb(e==asio::error::access_denied?CD_PERMISSION_DENIED:CD_CONNECTION_FAILED,e.message(),e.value());else {cb(CD_OK,{},0);pump();}
  }
 }
 void pump() {
  if(stopped) return; auto self=std::static_pointer_cast<StreamBackend>(shared_from_this());
  auto cb=[self](ErrorCode ec,size_t n){if(self->stopped) return;if(n||(!ec&&self->config.backend==1)) self->output(0,std::string(self->buffer.data(),n),false);if(ec) {self->output(0,{},true);if(ec!=asio::error::eof&&ec!=asio::error::operation_aborted) self->failure(CD_DISCONNECTED,ec.message(),ec.value());}else self->pump();};
  if(config.backend==0) tcp.async_read_some(asio::buffer(buffer),std::move(cb));
  else if(config.backend==1) udp.async_receive(asio::buffer(buffer),std::move(cb));else serial.async_read_some(asio::buffer(buffer),std::move(cb));
 }
 void write(std::string data,Completion cb) override {
  if(config.backend==1&&data.size()>65507) {cb(CD_INVALID_ARGUMENT,"UDP datagram exceeds 65507 bytes",0);return;}
  auto bytes=std::make_shared<std::string>(std::move(data)); auto self=shared_from_this();
  auto complete=[bytes,self,cb](ErrorCode ec,size_t){cb(ec?CD_IO_ERROR:CD_OK,ec?ec.message():"",ec.value());};
  if(config.backend==0) asio::async_write(tcp,asio::buffer(*bytes),std::move(complete));
  else if(config.backend==1) udp.async_send(asio::buffer(*bytes),std::move(complete));else asio::async_write(serial,asio::buffer(*bytes),std::move(complete));
 }
 void close() override {stopped=true;tcp_resolver.cancel();udp_resolver.cancel();ErrorCode e;tcp.close(e);udp.close(e);serial.close(e);}
 void close_input(Completion cb) override {if(config.backend!=0) {cb(CD_NOT_SUPPORTED,"Half-close unavailable",0);return;}ErrorCode ec;tcp.shutdown(asio::ip::tcp::socket::shutdown_send,ec);cb(ec?CD_IO_ERROR:CD_OK,ec.message(),ec.value());}
};

static std::pair<std::string,std::vector<std::string>> shell_command(int shell,bool one_shot,const std::string& command) {
#ifdef _WIN32
 if(shell==CD_SHELL_DEFAULT) shell=CD_SHELL_CMD;
#else
 if(shell==CD_SHELL_DEFAULT) shell=CD_SHELL_SH;
#endif
 if(shell==CD_SHELL_CMD) return {"cmd.exe",one_shot?std::vector<std::string>{"/D","/S","/C",command}:std::vector<std::string>{"/D","/Q"}};
 if(shell==CD_SHELL_WINDOWS_POWERSHELL||shell==CD_SHELL_PWSH) return {shell==CD_SHELL_PWSH?"pwsh":"powershell.exe",one_shot?std::vector<std::string>{"-NoLogo","-NoProfile","-NonInteractive","-Command",command}:std::vector<std::string>{"-NoLogo","-NoProfile","-NonInteractive","-Command","-"}};
 std::string exe=shell==CD_SHELL_BASH?"bash":shell==CD_SHELL_ZSH?"zsh":"sh";
 return {exe,one_shot?std::vector<std::string>{"-c",command}:std::vector<std::string>{}};
}
static process::filesystem::path executable_path(const std::string& executable) {
 auto path=utf8_path(executable);
 if(path.has_parent_path()) {if(!std::filesystem::is_regular_file(path))return {};return process::filesystem::path(std::filesystem::absolute(path).native());}
 return process::environment::find_executable(executable);
}
std::filesystem::path utf8_path(const std::string& s) {return std::filesystem::path(std::u8string(s.begin(),s.end()));}
bool Backend::filter_prompt(std::string& data) {
 if(prompt_token.empty())return true;prompt_buffer+=data;
 if(prompt_ready){auto pos=prompt_buffer.find(prompt_token);if(pos==std::string::npos){if(prompt_buffer.size()>config.defaults.max_bytes){auto cb=std::exchange(prompt_ready,{});cb(CD_LIMIT_EXCEEDED,"CMD startup exceeded buffer limit",0);}return false;}prompt_buffer.erase(0,pos+prompt_token.size());auto cb=std::exchange(prompt_ready,{});cb(CD_OK,{},0);}
 auto marker_line=[&](size_t end){if(completion_marker.empty())return false;auto prefix=output_tail+prompt_buffer.substr(0,end);auto marker=prefix.rfind(completion_marker+":");if(marker==std::string::npos)return false;auto status=prefix.substr(marker+completion_marker.size()+1);int code;auto parsed=std::from_chars(status.data(),status.data()+status.size(),code);return !status.empty()&&parsed.ec==std::errc()&&parsed.ptr==status.data()+status.size();};
 size_t pos;while((pos=prompt_buffer.find(prompt_token))!=std::string::npos){size_t start=pos>=2&&prompt_buffer.compare(pos-2,2,"\r\n")==0&&!marker_line(pos-2)?pos-2:pos;prompt_buffer.erase(start,pos-start+prompt_token.size());}
 size_t suffix=0;for(size_t n=1;n<prompt_token.size()&&n<=prompt_buffer.size();++n)if(prompt_buffer.compare(prompt_buffer.size()-n,n,prompt_token.substr(0,n))==0)suffix=n;
 if(suffix&&prompt_buffer.size()>=suffix+2&&prompt_buffer.compare(prompt_buffer.size()-suffix-2,2,"\r\n")==0)suffix+=2;
 if(!suffix&&prompt_buffer.ends_with("\r\n")&&!marker_line(prompt_buffer.size()-2))suffix=2;else if(!suffix&&prompt_buffer.ends_with('\r'))suffix=1;
 data=prompt_buffer.substr(0,prompt_buffer.size()-suffix);prompt_buffer.erase(0,prompt_buffer.size()-suffix);
 output_tail+=data;auto limit=completion_marker.size()+64;if(output_tail.size()>limit)output_tail.erase(0,output_tail.size()-limit);return true;
}
struct ProcessBackend : Backend {
 asio::writable_pipe input; asio::readable_pipe out,err; std::unique_ptr<process::process> child;
 std::array<char,8192> out_buffer{},err_buffer{}; bool stopped=false;
 std::shared_ptr<ProcessBackend> auxiliary; Completion auxiliary_callback;
 std::string auxiliary_output; bool auxiliary_eof[2]={false,false};std::optional<int> auxiliary_exit;
 ProcessBackend(std::shared_ptr<Runtime> r,Config c):Backend(r,std::move(c)),input(r->state->io),out(r->state->io),err(r->state->io){}
 std::vector<std::string> adb_args() {return {"-H",config.server_host,"-P",std::to_string(config.server_port),"-s",config.device};}
 void launch(const std::string& command,bool one,Completion cb) {
  try {
   std::string executable;std::vector<std::string> args;
   if(config.backend==4) {executable=config.executable.empty()?"adb":config.executable;args=adb_args();args.insert(args.end(),{"shell","-T"});if(one) args.push_back(command);else args.push_back(config.session_command.empty()?"sh":config.session_command);}
   else if(!config.executable.empty()) {executable=config.executable;args=config.arguments;}
   else {auto s=shell_command(config.shell,one,command);executable=s.first;args=std::move(s.second);}
   auto path=executable_path(executable);
   if(path.empty()) throw Fault(config.backend==4?CD_DEVICE_UNAVAILABLE:CD_CONNECTION_FAILED,"Executable not found: "+executable);
   std::map<std::string,std::string> env;
   for(auto v:process::environment::current()) {if(v.key().size()==0)continue;env[v.key().string()]=v.value().size()==0?std::string():v.value().string();}
   for(auto& [k,v]:config.environment) env[k]=v;
   bool cmd_shell=config.backend==5&&config.executable.empty()&&!one&&(config.shell==CD_SHELL_CMD
#ifdef _WIN32
    ||config.shell==CD_SHELL_DEFAULT
#endif
   );
   if(cmd_shell){std::random_device random;prompt_token="__CD_PROMPT_"+std::to_string(random())+std::to_string(random())+"__";env["PROMPT"]=prompt_token;prompt_ready=cb;}
   std::vector<std::string> env_list;for(auto& [k,v]:env) env_list.push_back(k+"="+v);
   auto dir=config.working_directory.empty()?std::filesystem::current_path():utf8_path(config.working_directory);
   child=std::make_unique<process::process>(runtime->state->io,path,args,process::process_stdio{input,out,err},process::process_environment(env_list),process::process_start_dir(process::filesystem::path(dir.native())));
   pump(0);pump(1); auto self=std::static_pointer_cast<ProcessBackend>(shared_from_this());
   child->async_wait([self](ErrorCode ec,int code){if(self->stopped) return;if(ec) self->failure(CD_IO_ERROR,ec.message(),ec.value());else self->exited(code);});
   if(!cmd_shell)cb(CD_OK,{},0);
  } catch(const Fault& f) {cb(f.code,f.message,f.native);}catch(const std::exception& e) {cb(CD_CONNECTION_FAILED,e.what(),0);}
 }
 void auxiliary_finish(cd_status_t status,std::string message,int64_t native) {
  if(!auxiliary_callback)return;auto cb=std::exchange(auxiliary_callback,{});
  if(auxiliary){auxiliary->close();auxiliary.reset();}cb(status,std::move(message),native);
 }
 void auxiliary_complete() {
  if(auxiliary_exit&&auxiliary_eof[0]&&auxiliary_eof[1]) auxiliary_finish(*auxiliary_exit==0?CD_OK:CD_IO_ERROR,auxiliary_output,*auxiliary_exit);
 }
 void run_auxiliary(std::vector<std::string> args,Completion cb) {
  auxiliary_callback=std::move(cb);auxiliary_output.clear();auxiliary_eof[0]=auxiliary_eof[1]=false;auxiliary_exit.reset();
  Config options=config;options.backend=5;options.mode=CD_ONE_SHOT;options.executable=config.executable.empty()?"adb":config.executable;options.arguments=std::move(args);
  auxiliary=std::make_shared<ProcessBackend>(runtime,std::move(options));auto weak=std::weak_ptr<ProcessBackend>(std::static_pointer_cast<ProcessBackend>(shared_from_this()));
  auxiliary->output=[weak](int stream,std::string data,bool end){if(auto self=weak.lock()){self->auxiliary_eof[stream]|=end;self->auxiliary_output+=data;if(self->auxiliary_output.size()>self->config.defaults.max_bytes)self->auxiliary_finish(CD_LIMIT_EXCEEDED,"ADB control output exceeded limit",0);else self->auxiliary_complete();}};
  auxiliary->exited=[weak](std::optional<int> code){if(auto self=weak.lock()){self->auxiliary_exit=code;self->auxiliary_complete();}};
  auxiliary->failure=[weak](cd_status_t s,std::string m,int64_t n){if(auto self=weak.lock())self->auxiliary_finish(s,m,n);};
  auxiliary->command({},[weak](cd_status_t s,std::string m,int64_t n){if(s!=CD_OK)if(auto self=weak.lock())self->auxiliary_finish(s,m,n);});
 }
 void open(Completion cb) override {
  if(config.backend==4){auto args=adb_args();args.push_back("get-state");auto self=std::static_pointer_cast<ProcessBackend>(shared_from_this());run_auxiliary(std::move(args),[self,cb](cd_status_t s,std::string output,int64_t native){
   if(s!=CD_OK||(output!="device\n"&&output!="device\r\n"&&output!="device")){cb(CD_DEVICE_UNAVAILABLE,"Bound ADB device is unavailable: "+output,native);return;}
   if(self->config.mode==CD_ONE_SHOT)cb(CD_OK,{},0);else self->launch({},false,cb);
  });return;}
  if(config.mode==CD_ONE_SHOT){auto executable=config.executable.empty()?shell_command(config.shell,true,{}).first:config.executable;if(executable_path(executable).empty())cb(CD_CONNECTION_FAILED,"Executable not found: "+executable,0);else cb(CD_OK,{},0);return;}
  launch({},false,std::move(cb));
 }
 bool command_channels() const override {return config.mode==CD_ONE_SHOT;}
 void command(std::string command,Completion cb) override {
  bool direct=config.backend==5&&!config.executable.empty();
  auto self=std::static_pointer_cast<ProcessBackend>(shared_from_this());
  launch(command,true,[self,direct,command,cb](cd_status_t s,std::string m,int64_t n){
   if(s!=CD_OK) {cb(s,m,n);return;}
   if(direct) self->write(command,[self,cb](cd_status_t status,std::string message,int64_t native){if(status!=CD_OK) cb(status,message,native);else self->close_input(cb);});
   else self->close_input(cb);
  });
 }
 void pump(int stream) {
  auto self=std::static_pointer_cast<ProcessBackend>(shared_from_this());auto& pipe=stream?err:out;auto& b=stream?err_buffer:out_buffer;
  pipe.async_read_some(asio::buffer(b),[self,stream](ErrorCode ec,size_t n){if(self->stopped) return;auto& buffer=stream?self->err_buffer:self->out_buffer;if(n) {
   std::string data(buffer.data(),n);
   if(stream==0&&!self->filter_prompt(data)){self->pump(stream);return;}
   if(!data.empty())self->output(stream,std::move(data),false);
  }if(ec) {if(self->prompt_ready){auto cb=std::exchange(self->prompt_ready,{});cb(CD_CONNECTION_FAILED,"CMD exited before managed prompt handshake",ec.value());}self->output(stream,{},true);if(ec!=asio::error::eof&&ec!=asio::error::broken_pipe&&ec!=asio::error::operation_aborted) self->failure(CD_IO_ERROR,ec.message(),ec.value());}else self->pump(stream);});
 }
 void write(std::string data,Completion cb) override {auto bytes=std::make_shared<std::string>(std::move(data));auto self=shared_from_this();asio::async_write(input,asio::buffer(*bytes),[self,bytes,cb](ErrorCode ec,size_t){cb(ec?CD_IO_ERROR:CD_OK,ec.message(),ec.value());});}
 void close_input(Completion cb) override {ErrorCode e;input.close(e);cb(e?CD_IO_ERROR:CD_OK,e.message(),e.value());}
 void transfer(bool upload,std::string source,std::string destination,bool overwrite,std::function<void(uint64_t)> progress,Completion cb) override {
  if(config.backend!=4){cb(CD_NOT_SUPPORTED,"CLI has no file transfer",0);return;}
  transfer_partial_file.clear();
  try {
   if(!upload&&std::filesystem::is_directory(utf8_path(destination)))throw Fault(CD_INVALID_ARGUMENT,"Destination must be a single file");
   if(!upload&&!overwrite&&std::filesystem::exists(utf8_path(destination)))throw Fault(CD_PERMISSION_DENIED,"Destination already exists");
   if(upload&&!std::filesystem::is_regular_file(utf8_path(source)))throw Fault(CD_INVALID_ARGUMENT,"Source must be a single regular file");
   auto self=std::static_pointer_cast<ProcessBackend>(shared_from_this());
   auto quote=[](const std::string& value){std::string text="'";for(char c:value)text+=c=='\''?"'\\''":std::string(1,c);return text+"'";};
   std::random_device random;auto staging=overwrite?destination:destination+".cd-partial-"+std::to_string(random())+std::to_string(random());
   auto perform=[self,upload,source,destination,staging,overwrite,progress,cb,quote]{self->transfer_partial_file=staging;auto args=self->adb_args();args.push_back(upload?"push":"pull");args.push_back(source);args.push_back(staging);
    self->run_auxiliary(std::move(args),[self,upload,source,destination,staging,overwrite,progress,cb,quote](cd_status_t s,std::string m,int64_t n){
     if(s!=CD_OK){cb(s,std::move(m),n);return;}
     auto done=[self,upload,source,destination,progress,cb](cd_status_t status,std::string message,int64_t native){if(status==CD_OK){self->transfer_partial_file.clear();std::error_code e;auto size=std::filesystem::file_size(utf8_path(upload?source:destination),e);if(!e)progress(size);}cb(status,std::move(message),native);};
     if(overwrite){done(CD_OK,{},0);return;}
     if(upload){auto install=self->adb_args();install.insert(install.end(),{"shell","-T","ln "+quote(staging)+" "+quote(destination)+" && rm "+quote(staging)});self->run_auxiliary(std::move(install),std::move(done));}
     else {std::error_code error;std::filesystem::create_hard_link(utf8_path(staging),utf8_path(destination),error);if(error){done(error==std::errc::file_exists?CD_PERMISSION_DENIED:CD_IO_ERROR,error.message(),error.value());return;}std::filesystem::remove(utf8_path(staging),error);done(error?CD_IO_ERROR:CD_OK,error.message(),error.value());}
    });};
   auto args=adb_args();args.insert(args.end(),{"shell","-T",std::string(upload?"test -d ":"test -f ")+quote(upload?destination:source)});
   run_auxiliary(std::move(args),[self,upload,overwrite,destination,perform,cb,quote](cd_status_t s,std::string m,int64_t code){if((upload&&s==CD_OK)||(!upload&&code==1)){cb(CD_INVALID_ARGUMENT,"Transfer requires a single file path",0);return;}if((upload&&code!=1)||(!upload&&s!=CD_OK)){cb(s,std::move(m),code);return;}
    if(upload&&!overwrite){auto probe=self->adb_args();probe.insert(probe.end(),{"shell","-T","test -e "+quote(destination)});self->run_auxiliary(std::move(probe),[perform,cb](cd_status_t status,std::string message,int64_t native){if(status==CD_OK)cb(CD_PERMISSION_DENIED,"Destination already exists",0);else if(native==1)perform();else cb(status,std::move(message),native);});}else perform();});
  }catch(const Fault& f){cb(f.code,f.message,f.native);}catch(const std::exception& e){cb(CD_IO_ERROR,e.what(),0);}
 }
 void close() override {stopped=true;if(auxiliary){auxiliary->close();auxiliary.reset();}auxiliary_callback={};ErrorCode e;input.close(e);out.close(e);err.close(e);if(child) {child->terminate(e);child.reset();}}
};
std::shared_ptr<Backend> make_backend(std::shared_ptr<Runtime> r,Config c) {if(c.backend==3) return make_ssh(r,std::move(c));if(c.backend>=4) return std::make_shared<ProcessBackend>(r,std::move(c));return std::make_shared<StreamBackend>(r,std::move(c));}
Connection::Connection(std::shared_ptr<Runtime> r,Config c):runtime(std::move(r)),config(std::move(c)),capabilities(CD_CAP_READ|CD_CAP_WRITE|CD_CAP_EXECUTE|CD_CAP_EXECUTE_UNTIL) {
 if(config.backend==0||config.backend>=3) capabilities|=CD_CAP_CLOSE_INPUT;
 if(config.backend>=3) capabilities|=CD_CAP_STDERR;
 if(config.backend==3||config.backend==4) capabilities|=CD_CAP_UPLOAD|CD_CAP_DOWNLOAD;
 if(config.backend==1) capabilities|=CD_CAP_MESSAGE;
}
Connection::~Connection() {if(persistent) persistent->close();if(one_shot) one_shot->close();}
void Connection::attach(const std::shared_ptr<Backend>& backend,bool isolated) {
 auto weak=weak_from_this();uint64_t g=generation;
 backend->output=[weak,g,isolated](int stream,std::string data,bool end){if(auto self=weak.lock();self&&self->generation==g) self->receive(stream,std::move(data),end,isolated);};
 backend->failure=[weak,g](cd_status_t s,std::string m,int64_t n){if(auto self=weak.lock();self&&self->generation==g) {self->state=CD_STATE_FAULTED;if(self->active) self->finish(s,std::move(m),n);}};
 backend->exited=[weak,g,isolated](std::optional<int> code){if(auto self=weak.lock();self&&self->generation==g&&(!self->isolating||isolated)) {self->process_exit=code;self->process_finished=true;self->process_data();}};
 backend->command_output=[weak,g](int stream,std::string data,bool end){if(auto self=weak.lock();self&&self->generation==g)self->receive(stream,std::move(data),end,true);};
 backend->command_exited=[weak,g](std::optional<int> code){if(auto self=weak.lock();self&&self->generation==g){self->process_exit=code;self->process_finished=true;self->process_data();}};
}
void Connection::submit(std::shared_ptr<Request> request) {
 auto self=shared_from_this(); auto op=request->operation;
 {std::lock_guard lock(op->mutex);op->cancel_hook=[weak=weak_from_this(),w=std::weak_ptr<Request>(request)]{if(auto c=weak.lock()) asio::post(c->runtime->state->io,[c,w]{if(auto r=w.lock()) c->abort(r,CD_CANCELLED,"Operation cancelled");});};}
 if(request->options.cancellation) request->options.cancellation->subscribe(op);
 asio::post(runtime->state->io,[self,request]{
  request->deadline=std::make_shared<asio::steady_timer>(self->runtime->state->io);
  request->deadline->expires_at(request->started+std::chrono::milliseconds(request->options.timeout));
  request->deadline->async_wait([weak=self->weak_from_this(),w=std::weak_ptr<Request>(request)](ErrorCode e){if(!e) if(auto c=weak.lock()) if(auto r=w.lock()) c->abort(r,CD_TIMEOUT,"Operation timed out");});
  if(request->action==Action::Close){self->force_close();self->active=request;self->force_close();return;}
  self->queue.push_back(request);self->next();
 });
}
void Connection::next() {if(active||queue.empty()) return;active=queue.front();queue.pop_front();try{start();}catch(const Fault& error){finish(error.code,error.message,error.native);}catch(const std::exception& error){finish(CD_INTERNAL_ERROR,error.what());}}
void Connection::ensure_persistent(Completion callback) {
 if(persistent&&!(config.backend>=4&&persistent->command_channels())) {persistent->ensure_stdio(std::move(callback));return;}
 if(persistent){persistent->close();persistent.reset();}
 auto c=config;c.mode=CD_PERSISTENT;persistent=make_backend(runtime,c);attach(persistent);
 persistent->open(std::move(callback));
}
void Connection::start() {
 auto r=active;auto self=shared_from_this();r->result->backend=config.name();
 if(r->operation->cancelled) {finish(CD_CANCELLED,"Operation cancelled");return;}
 if(runtime->closed&&r->action!=Action::Close) {finish(CD_CLOSED,"Driver is closed");return;}
 if(r->action==Action::Close) {force_close();return;}
 if(r->action==Action::Open) {
  if(state==CD_STATE_OPEN) {finish();return;}
  if(state==CD_STATE_NEEDS_RECOVERY||state==CD_STATE_FAULTED) {finish(CD_NEEDS_RECOVERY,"Close before reopening");return;}
  state=CD_STATE_OPENING;++generation;pending[0].clear();pending[1].clear();messages[0].clear();messages[1].clear();eof[0]=eof[1]=false;process_exit.reset();process_finished=false;
  persistent=make_backend(runtime,config);attach(persistent);
  persistent->open([self,r](cd_status_t s,std::string m,int64_t n){if(self->active!=r) return;self->state=s==CD_OK?CD_STATE_OPEN:CD_STATE_FAULTED;self->finish(s,std::move(m),n);});return;
 }
 if(state!=CD_STATE_OPEN) {finish(state==CD_STATE_NEEDS_RECOVERY?CD_NEEDS_RECOVERY:state==CD_STATE_CLOSED?CD_CLOSED:CD_NOT_OPEN,"Connection is not ready");return;}
 if(!(capabilities&CD_CAP_STDERR)&&(r->options.read_stream||r->options.check_stream)) {finish(CD_NOT_SUPPORTED,"Backend has no independent stderr");return;}
 if((r->action==Action::Upload||r->action==Action::Download)&&!(capabilities&CD_CAP_UPLOAD)) {finish(CD_NOT_SUPPORTED,"Backend has no file transfer");return;}
 if(r->action==Action::CloseInput&&!(capabilities&CD_CAP_CLOSE_INPUT)) {finish(CD_NOT_SUPPORTED,"Backend has no input EOF");return;}
 if(r->options.rule.kind==CD_RECEIVE_MESSAGE&&!(capabilities&CD_CAP_MESSAGE)) {finish(CD_NOT_SUPPORTED,"Backend has no native message boundary");return;}
 bool exec=r->action==Action::Execute||r->action==Action::ExecuteUntil;
 bool managed=config.managed();
 if(r->action==Action::Execute&&!managed&&!(config.backend>=3&&config.mode==CD_ONE_SHOT)&& (r->options.rule.kind==CD_RECEIVE_NONE||r->options.rule.kind==CD_RECEIVE_AVAILABLE)) {finish(CD_INVALID_ARGUMENT,"Execute requires a reliable ReceiveUntil rule before sending");return;}
 if(r->action==Action::ExecuteUntil&&(r->check.kind==CD_CHECK_NONE||r->check.kind==CD_CHECK_EQUALS)) {finish(CD_INVALID_ARGUMENT,"ExecuteUntil requires Contains or Regex");return;}
 if(exec&&!(config.backend>=3&&config.mode==CD_ONE_SHOT)&&(!pending[0].empty()||!pending[1].empty())) {state=CD_STATE_NEEDS_RECOVERY;finish(CD_NEEDS_RECOVERY,"Unattributed output exists; consume it explicitly or close/reopen");return;}
 if(exec) r->result->kind=r->text?CD_RESULT_EXECUTION_TEXT:CD_RESULT_EXECUTION_BYTES;
 else if(r->action==Action::Read) r->result->kind=r->text?CD_RESULT_TEXT:CD_RESULT_BYTES;
 else if(r->action==Action::Upload||r->action==Action::Download) r->result->kind=CD_RESULT_TRANSFER;
 if(r->action==Action::Upload||r->action==Action::Download) {
  r->result->source=r->source;r->result->destination=r->destination;
  r->sent=true;persistent->transfer(r->action==Action::Upload,r->source,r->destination,r->overwrite,[weak=std::weak_ptr<Request>(r)](uint64_t bytes){if(auto req=weak.lock()) req->result->progress=bytes;},[self,r](cd_status_t status,std::string message,int64_t native){if(self->active==r) {if(status!=CD_OK){r->result->partial_file=self->persistent->transfer_partial_file;if(status!=CD_PERMISSION_DENIED&&status!=CD_INVALID_ARGUMENT){self->state=CD_STATE_NEEDS_RECOVERY;++self->generation;self->persistent->close();}}self->finish(status,message,native);}});return;
 }
 if(exec&&config.backend>=3&&config.mode==CD_ONE_SHOT) {
  isolating=true;for(int stream=0;stream<2;++stream){stdio_pending[stream]=std::exchange(pending[stream],{});stdio_eof[stream]=eof[stream];}
  eof[0]=eof[1]=false;process_exit.reset();process_finished=false;
  if(config.backend==3) {one_shot=persistent;}
  else {one_shot=make_backend(runtime,config);attach(one_shot,true);}
  r->sent=true;r->writing=true;
  one_shot->command(r->input,[self,r](cd_status_t s,std::string m,int64_t n){if(self->active!=r) return;r->writing=false;if(s!=CD_OK) self->finish(s,m,n);else self->process_data();});return;
 }
 ensure_persistent([self,r,exec,managed](cd_status_t s,std::string m,int64_t n){
  if(self->active!=r) return;if(s!=CD_OK) {self->finish(s,m,n);return;}
  if(r->action==Action::CloseInput) {self->persistent->close_input([self,r](cd_status_t status,std::string message,int64_t native){if(self->active==r) self->finish(status,message,native);});return;}
  if(r->action==Action::Read) {self->process_data();return;}
  std::string data=r->input;
  if(exec&&managed) {std::random_device random;std::ostringstream marker;marker<<"__CD_"<<std::hex<<random()<<random()<<random()<<"__";r->marker=marker.str();self->persistent->completion_marker=r->marker;data=self->framed_command(data,r->marker);}
  r->sent=true;r->writing=true;
  self->persistent->write(std::move(data),[self,r,exec](cd_status_t status,std::string message,int64_t native){if(self->active!=r) return;r->writing=false;if(status!=CD_OK) self->finish(status,message,native);else if(!exec) self->finish();else self->process_data();});
 });
}
std::string Connection::framed_command(const std::string& command,const std::string& marker) {
 int shell=config.shell;
#ifdef _WIN32
 if(config.backend==5&&shell==CD_SHELL_DEFAULT) shell=CD_SHELL_CMD;
#endif
 if((config.backend==3||config.backend==5)&&shell==CD_SHELL_CMD) return command+"\r\nset \"__cd_exit=%errorlevel%\"\r\necho "+marker+":%__cd_exit%\r\necho "+marker+":%__cd_exit% 1>&2\r\n";
 if((config.backend==3||config.backend==5)&&(shell==CD_SHELL_PWSH||shell==CD_SHELL_WINDOWS_POWERSHELL)) return command+"\n$__cd_exit = if ($?) { 0 } else { if ($LASTEXITCODE) { $LASTEXITCODE } else { 1 } }; [Console]::Out.Write('"+marker+":'+$__cd_exit+\"`n\"); [Console]::Error.Write('"+marker+":'+$__cd_exit+\"`n\")\n";
 // eval preserves cd/env in the current shell while keeping arbitrary multi-line commands intact.
 std::string quoted="'";for(char c:command) quoted+=c=='\''?"'\\''":std::string(1,c);quoted+="'";
 return "eval "+quoted+"\n__cd_exit=$?; printf '"+marker+":%s\\n' \"$__cd_exit\"; printf '"+marker+":%s\\n' \"$__cd_exit\" >&2\n";
}
void Connection::receive(int stream,std::string data,bool end,bool isolated) {
 auto r=active;
 if(isolated&&(!r||(r->action!=Action::Execute&&r->action!=Action::ExecuteUntil))) return;
 if(isolating&&!isolated){stdio_pending[stream]+=data;stdio_eof[stream]|=end;if(stdio_pending[0].size()+stdio_pending[1].size()>config.defaults.max_bytes){state=CD_STATE_NEEDS_RECOVERY;if(persistent)persistent->close();finish(CD_LIMIT_EXCEEDED,"Persistent stdio buffer limit exceeded");}return;}
 if(end) eof[stream]=true;
 if(config.backend==1&&!end) messages[stream].push_back(data.size());
 pending[stream]+=data;
 uint64_t limit=r?r->options.max_bytes:config.defaults.max_bytes;
 size_t total=pending[0].size()+pending[1].size();if(r) total+=r->result->raw[0].size()+r->result->raw[1].size();
 if(total>limit||messages[0].size()+messages[1].size()>std::max<uint64_t>(1024,limit/sizeof(size_t))) {state=CD_STATE_NEEDS_RECOVERY;if(r) {r->result->raw[stream]+=pending[stream];pending[stream].clear();finish(CD_LIMIT_EXCEEDED,"Response buffer limit exceeded");}if(persistent) persistent->close();if(one_shot) one_shot->close();return;}
 process_data();
}
bool Connection::boundary(std::string& data,const Rule& rule,const Options& options,size_t& consume,size_t& keep) {
 consume=keep=0;
 switch(rule.kind) {
 case CD_RECEIVE_BYTES: if(data.size()>=rule.count) {consume=keep=static_cast<size_t>(rule.count);return true;}return false;
 case CD_RECEIVE_CHARACTERS: {auto text=decode(data,options.encoding,false);auto pos=scalar_prefix(text,rule.count);if(pos==std::string::npos) return false;consume=keep=encode(text.substr(0,pos),options.encoding).size();return true;}
 case CD_RECEIVE_DELIMITER: {auto delimiter=rule.text?encode(rule.value,options.encoding):rule.value;auto pos=data.find(delimiter);if(pos==std::string::npos) return false;consume=pos+delimiter.size();keep=rule.include?consume:pos;return true;}
 case CD_RECEIVE_PATTERN: {auto text=decode(data,options.encoding,false);absl::string_view match;if(!rule.regex->Match(text,0,text.size(),RE2::UNANCHORED,&match,1)) return false;size_t end=static_cast<size_t>(match.data()-text.data())+match.size();consume=keep=encode(text.substr(0,end),options.encoding).size();return true;}
 case CD_RECEIVE_AVAILABLE: if(!data.empty()) {consume=keep=data.size();return true;}return false;
 case CD_RECEIVE_MESSAGE: {auto& sizes=messages[options.read_stream];if(sizes.empty()) return false;consume=keep=sizes.front();return true;}
 default: return false;
 }
}
void Connection::consume_pending(int stream,size_t count) {
 pending[stream].erase(0,count);auto& sizes=messages[stream];
 while(!sizes.empty()) {if(sizes.front()>count){sizes.front()-=count;break;}count-=sizes.front();sizes.pop_front();if(!count)break;}
}
void Connection::process_data() {
 auto r=active;if(!r||r->writing) return;
 try {
  if(r->action==Action::Read) {
   int stream=r->options.read_stream;size_t consume=0,keep=0;
   if(boundary(pending[stream],r->options.rule,r->options,consume,keep)) {
    if(r->text&&r->options.rule.kind==CD_RECEIVE_AVAILABLE) {auto text=decode(pending[stream],r->options.encoding,false);if(text.empty()&&!pending[stream].empty()) {if(eof[stream]) throw Fault(CD_ENCODING_ERROR,"Incomplete terminal scalar");return;}consume=keep=encode(text,r->options.encoding).size();}
    r->result->raw[stream]=pending[stream].substr(0,keep);consume_pending(stream,consume);finish();return;
   }
   if(r->options.rule.kind==CD_RECEIVE_IDLE&&!pending[stream].empty()) {
    r->result->raw[stream]+=pending[stream];consume_pending(stream,pending[stream].size());
    if(!r->idle) r->idle=std::make_shared<asio::steady_timer>(runtime->state->io);
    r->idle->expires_after(std::chrono::milliseconds(r->options.rule.idle_ms));auto self=shared_from_this();r->idle->async_wait([self,r](ErrorCode e){if(!e&&self->active==r) {r->result->completion=CD_COMPLETION_IDLE;self->finish();}});
   }
   if(eof[stream]) {r->result->raw[stream]+=std::exchange(pending[stream],{});finish(CD_END_OF_STREAM,"Input reached EOF");}return;
  }
  if(r->action!=Action::Execute&&r->action!=Action::ExecuteUntil) return;
  for(int stream=0;stream<2;++stream) {
   if(!r->marker.empty()) {
    auto& data=pending[stream];auto pos=data.find(r->marker+":");
    if(pos!=std::string::npos) {auto end=data.find('\n',pos);if(end!=std::string::npos) {auto exit_text=data.substr(pos+r->marker.size()+1,end-pos-r->marker.size()-1);if(!exit_text.empty()&&exit_text.back()=='\r') exit_text.pop_back();int code=0;auto parsed=std::from_chars(exit_text.data(),exit_text.data()+exit_text.size(),code);if(parsed.ec!=std::errc()) throw Fault(CD_PROTOCOL_ERROR,"Invalid internal completion marker");r->result->raw[stream]+=data.substr(0,pos);data.erase(0,end+1);r->marker_mask|=1<<stream;r->marker_exit=code;}}
    // Retain only the possible marker suffix, so checks can observe output immediately.
    if(!(r->marker_mask&(1<<stream))) {size_t retain=0;for(size_t n=1;n<=std::min(data.size(),r->marker.size()+1);++n) if(data.compare(data.size()-n,n,(r->marker+":").substr(0,n))==0) retain=n;auto prefix=data.size()-retain;if(data.find(r->marker+":")==std::string::npos) {r->result->raw[stream]+=data.substr(0,prefix);data.erase(0,prefix);}}
   } else if(config.backend>=3&&config.mode==CD_ONE_SHOT) r->result->raw[stream]+=std::exchange(pending[stream],{});
  }
  bool command_done=!r->marker.empty()?r->marker_mask==3:(config.backend>=3&&config.mode==CD_ONE_SHOT&&process_finished&&eof[0]&&eof[1]);
  bool raw_until=r->action==Action::ExecuteUntil&&r->marker.empty()&&!(config.backend>=3&&config.mode==CD_ONE_SHOT);
  bool raw_done=false;size_t raw_consume=0,raw_keep=0;
  if(raw_until) {
   r->result->raw[0]+=std::exchange(pending[0],{});r->result->raw[1]+=std::exchange(pending[1],{});
   if(r->options.rule.kind!=CD_RECEIVE_NONE&&r->options.rule.kind!=CD_RECEIVE_AVAILABLE&&r->options.rule.kind!=CD_RECEIVE_IDLE) raw_done=boundary(r->result->raw[0],r->options.rule,r->options,raw_consume,raw_keep);
  }
  if(r->action==Action::ExecuteUntil&&!r->early) {
   r->result->evaluate(r->check,r->options.check_stream,r->options.encoding,false);
   if(r->result->passed) {
    r->result->completion=CD_COMPLETION_MATCH;
    bool cleanup=!r->marker.empty()||(config.backend>=3&&config.mode==CD_ONE_SHOT)||(raw_until&&r->options.rule.kind!=CD_RECEIVE_NONE&&r->options.rule.kind!=CD_RECEIVE_AVAILABLE&&r->options.rule.kind!=CD_RECEIVE_IDLE);
    if(cleanup&&!command_done&&!raw_done) {r->early=true;auto delivered=std::make_shared<Result>(*r->result);delivered->elapsed_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-r->started).count();if(r->text) for(int i=0;i<2;++i) delivered->text[i]=decode(delivered->raw[i],r->options.encoding,false);r->operation->complete(delivered);}
    else {if(raw_until&&!cleanup)state=CD_STATE_NEEDS_RECOVERY;if(!raw_done){messages[0].clear();messages[1].clear();finish();return;}}
   }
  }
  if(raw_until&&raw_done) {
   pending[0]=r->result->raw[0].substr(raw_consume)+pending[0];r->result->raw[0].resize(raw_keep);
   size_t left=raw_consume;while(!messages[0].empty()){if(messages[0].front()>left){messages[0].front()-=left;break;}left-=messages[0].front();messages[0].pop_front();if(!left)break;}
   if(r->early||r->result->passed)finish();else finish(CD_END_OF_STREAM,"Protocol response completed without matching");return;
  }
  if(command_done) {r->result->exit_code=r->marker.empty()?process_exit:r->marker_exit;if(!r->early&&r->action==Action::ExecuteUntil&&!r->result->passed) {finish(CD_END_OF_STREAM,"Command completed without matching");return;}if(!r->early&&r->result->completion!=CD_COMPLETION_MATCH) r->result->completion=CD_COMPLETION_COMMAND;finish();return;}
  if(r->marker.empty()&&!(config.backend>=3&&config.mode==CD_ONE_SHOT)&&r->action==Action::Execute) {
   size_t consume=0,keep=0;int stream=0;
   if(boundary(pending[stream],r->options.rule,r->options,consume,keep)) {r->result->raw[stream]=pending[stream].substr(0,keep);consume_pending(stream,consume);r->result->completion=CD_COMPLETION_PROTOCOL;finish();return;}
   if(r->options.rule.kind==CD_RECEIVE_IDLE&&!pending[0].empty()) {r->result->raw[0]+=std::exchange(pending[0],{});if(!r->idle) r->idle=std::make_shared<asio::steady_timer>(runtime->state->io);r->idle->expires_after(std::chrono::milliseconds(r->options.rule.idle_ms));auto self=shared_from_this();r->idle->async_wait([self,r](ErrorCode e){if(!e&&self->active==r) {r->result->completion=CD_COMPLETION_IDLE;self->finish();}});}
  }
  bool input_ended=eof[0]&&(!(capabilities&CD_CAP_STDERR)||eof[1]);
  if(input_ended&&!command_done&&!(config.backend>=3&&config.mode==CD_ONE_SHOT)) finish(CD_END_OF_STREAM,"Input reached EOF before completion");
 }catch(const Fault& f) {r->result->raw[0]+=std::exchange(pending[0],{});r->result->raw[1]+=std::exchange(pending[1],{});if(r->sent)state=CD_STATE_NEEDS_RECOVERY;finish(f.code,f.message,f.native);}catch(const std::exception& e) {finish(CD_INTERNAL_ERROR,e.what());}
}
void Connection::finish(cd_status_t status,std::string message,int64_t native,bool advance) {
 auto r=std::exchange(active,{});if(!r) return;
 r->deadline->cancel();if(r->idle) r->idle->cancel();
 r->result->status=status;r->result->message=std::move(message);r->result->native=native;
 r->result->elapsed_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-r->started).count();
 try {if(r->text) for(int i=0;i<2;++i) r->result->text[i]=decode(r->result->raw[i],r->options.encoding,status==CD_OK);if(status==CD_OK&&(r->action==Action::Execute||r->action==Action::ExecuteUntil)) r->result->evaluate(r->check,r->options.check_stream,r->options.encoding,true);}catch(const Fault& f) {if(status==CD_OK){r->result->status=f.code;r->result->message=f.message;r->result->native=f.native;}}
 if(!r->early) r->operation->complete(r->result);
 if(one_shot&&config.backend!=3) {one_shot->close();one_shot.reset();}
 if(isolating){for(int stream=0;stream<2;++stream){pending[stream]=std::exchange(stdio_pending[stream],{});eof[stream]=stdio_eof[stream];}isolating=false;}
 if(advance) {auto self=shared_from_this();asio::post(runtime->state->io,[self]{self->next();});}
}
void Connection::abort(const std::shared_ptr<Request>& r,cd_status_t status,std::string message) {
 if(r==active) {
  if(r->sent&&(r->action==Action::Execute||r->action==Action::ExecuteUntil||r->action==Action::Write)) state=CD_STATE_NEEDS_RECOVERY;
  r->result->raw[0]+=std::exchange(pending[0],{});r->result->raw[1]+=std::exchange(pending[1],{});
  if(r->action==Action::Upload||r->action==Action::Download) {if(r->sent&&persistent)r->result->partial_file=persistent->transfer_partial_file;state=CD_STATE_NEEDS_RECOVERY;++generation;if(persistent)persistent->close();}
  if(r->action==Action::Open) {state=CD_STATE_FAULTED;++generation;if(persistent) persistent->close();persistent.reset();}
  finish(status,std::move(message));
 }else {auto it=std::find(queue.begin(),queue.end(),r);if(it==queue.end()) return;queue.erase(it);r->deadline->cancel();r->result->status=status;r->result->message=std::move(message);r->result->elapsed_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-r->started).count();r->operation->complete(r->result);}
}
void Connection::force_close() {
 state=CD_STATE_CLOSING;++generation;if(persistent) persistent->close();if(one_shot&&one_shot!=persistent) one_shot->close();persistent.reset();one_shot.reset();
 auto queued=std::exchange(queue,{});
 auto closing=active&&active->action==Action::Close?active:nullptr;
 if(active&&!closing) {active->result->raw[0]+=std::exchange(pending[0],{});active->result->raw[1]+=std::exchange(pending[1],{});finish(CD_CLOSED,"Connection closed",0,false);}
 for(auto& r:queued) {r->deadline->cancel();r->result->status=CD_CLOSED;r->result->message="Connection closed";r->operation->complete(r->result);}
 pending[0].clear();pending[1].clear();messages[0].clear();messages[1].clear();
 stdio_pending[0].clear();stdio_pending[1].clear();isolating=false;
 if(closing){auto self=shared_from_this();runtime->after_files([self,closing]{self->state=CD_STATE_CLOSED;if(self->active==closing)self->finish();});}
 else state=CD_STATE_CLOSED;
}
}
