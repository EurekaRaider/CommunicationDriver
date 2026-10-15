#include <communicationdriver/communicationdriver.h>
#include <boost/asio.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#ifndef _WIN32
#include <fcntl.h>
#include <termios.h>
#include <sys/stat.h>
#include <fstream>
#include <unistd.h>
#endif
namespace asio=boost::asio;
using Clock=std::chrono::steady_clock;
static int checks=0;
static void expect(bool yes,const std::string& message) {++checks;if(std::getenv("CD_TEST_VERBOSE"))std::cout<<checks<<" "<<message<<std::endl;if(!yes)throw std::runtime_error(message);}
static cd_string_view_t sv(const std::string& s) {return {s.data(),s.size()};}
struct Result {
 cd_result_t* handle=nullptr;Result()=default;Result(const Result&)=delete;Result& operator=(const Result&)=delete;
 ~Result(){cd_result_release(handle);}std::string text(int stream=0) const {cd_string_view_t v{};cd_result_get_text(handle,stream,&v);return v.size?std::string(v.data,v.size):"";}
 std::string bytes(int stream=0) const {cd_bytes_view_t v{};cd_result_get_bytes(handle,stream,&v);return v.size?std::string(reinterpret_cast<const char*>(v.data),v.size):"";}
 cd_status_t status() const {return cd_result_get_status(handle);}
 std::string error() const {cd_error_info_t e{};e.struct_size=sizeof(e);cd_result_get_error(handle,&e);return e.message.size?std::string(e.message.data,e.message.size):"";}
 bool passed() const {cd_check_result_t c{};c.struct_size=sizeof(c);cd_result_get_check(handle,&c);return c.has_value&&c.passed;}
 cd_execution_info_t info() const {cd_execution_info_t i{};i.struct_size=sizeof(i);cd_result_get_execution_info(handle,&i);return i;}
};
static void ok(cd_status_t s,const Result& r,const char* context) {expect(s==CD_OK,std::string(context)+": "+cd_status_name(s)+" "+r.error());}
struct Driver {cd_driver_t* handle=nullptr;Driver(){expect(cd_driver_create(&handle)==CD_OK,"create driver");}~Driver(){cd_driver_release(handle);}};
struct Connection {cd_connection_t* handle=nullptr;Connection()=default;Connection(Connection&& c)noexcept:handle(std::exchange(c.handle,nullptr)){}~Connection(){cd_connection_release(handle);}void open(){Result r;ok(cd_open(handle,nullptr,&r.handle),r,"open");}void close(){Result r;ok(cd_close(handle,nullptr,&r.handle),r,"close");}};
static Connection cli(Driver& d,bool oneshot=false) {Connection c;cd_cli_options_t o;cd_cli_options_init(&o);o.execution_mode=oneshot?CD_ONE_SHOT:CD_PERSISTENT;Result e;ok(cd_create_cli(d.handle,&o,&c.handle,&e.handle),e,"create cli");c.open();return c;}
static void execute(Connection& c,const std::string& command,Result& r,const cd_check_t* check=nullptr,const cd_call_options_t* opts=nullptr) {ok(cd_execute_string(c.handle,sv(command),check,opts,&r.handle),r,"execute");}
static void wait(cd_operation_t* operation,Result& result) {expect(cd_operation_wait(operation,10000,&result.handle)==CD_OK,"operation completed");cd_operation_release(operation);}
struct Peers {
 asio::io_context io;asio::executor_work_guard<asio::io_context::executor_type> guard=asio::make_work_guard(io);
 asio::ip::tcp::acceptor listener{io,{asio::ip::tcp::v4(),0}};asio::ip::udp::socket udp{io,{asio::ip::udp::v4(),0}};
 std::thread thread;std::atomic<int> requests{0};
 struct Session:std::enable_shared_from_this<Session> {Peers& owner;asio::ip::tcp::socket socket;std::array<char,1024> buffer;Session(Peers& p,asio::ip::tcp::socket s):owner(p),socket(std::move(s)){}
  void read(){auto self=shared_from_this();socket.async_read_some(asio::buffer(buffer),[self](auto e,size_t n){if(e)return;std::string command(self->buffer.data(),n);++self->owner.requests;
   if(command=="UTF")self->send_chunks({"A\xf0\x9f","\x98\x84"});
   else if(command.rfind("BULK:",0)==0){self->socket.set_option(asio::ip::tcp::no_delay(true));self->send_bulk(std::stoull(command.substr(5)));}
   else if(command=="BAD")self->send_chunks({std::string("A\xff",2)});
   else if(command=="EOF"){auto bytes=std::make_shared<std::string>("PART");asio::async_write(self->socket,asio::buffer(*bytes),[self,bytes](auto error,size_t){if(!error){boost::system::error_code ignored;self->socket.shutdown(asio::ip::tcp::socket::shutdown_send,ignored);}});}
   else if(command=="LATE")self->send_chunks({"BUSY","READY"},150);
   else if(command=="FRAME")self->send_chunks({"NOT","\n"});
   else self->send_chunks({command});self->read();});}
  void send_bulk(size_t remaining){auto self=shared_from_this();auto bytes=std::make_shared<std::string>(remaining?std::min<size_t>(remaining,4096):0,'x');if(!remaining)*bytes="\r\nTAIL\r\n";asio::async_write(socket,asio::buffer(*bytes),[self,bytes,remaining](auto e,size_t){if(!e&&remaining)self->send_bulk(remaining-bytes->size());});}
  void send_chunks(std::vector<std::string> data,int delay=5){auto self=shared_from_this();for(size_t i=0;i<data.size();++i){auto timer=std::make_shared<asio::steady_timer>(owner.io,std::chrono::milliseconds(i*delay));auto bytes=std::make_shared<std::string>(data[i]);timer->async_wait([self,timer,bytes](auto e){if(!e)asio::async_write(self->socket,asio::buffer(*bytes),[self,bytes](auto,size_t){});});}}
 };
 std::array<char,65536> udp_buffer;asio::ip::udp::endpoint sender;
 Peers(){accept();receive();thread=std::thread([this]{io.run();});}
 ~Peers(){io.stop();thread.join();}
 void accept(){listener.async_accept([this](auto e,auto socket){if(!e)std::make_shared<Session>(*this,std::move(socket))->read();accept();});}
 void receive(){udp.async_receive_from(asio::buffer(udp_buffer),sender,[this](auto e,size_t n){if(e)return;std::string command(udp_buffer.data(),n);auto destination=sender;++requests;
  std::vector<std::string> chunks=command=="UDP"?std::vector<std::string>{"REA","DY"}:command=="UTF"?std::vector<std::string>{"\xe4","\xbd","\xa0\xf0\x9f","\x98\x84"}:command=="GB"?std::vector<std::string>{"\xc4","\xe3","\xba","\xc3"}:std::vector<std::string>{command};
  if(command=="UTFMATCH")chunks={"A\xe4","\xbd\xa0\xf0\x9f","\x98\x84READY","\r","\nTAIL"};
  if(command=="GBSCALAR")chunks={"A\xc4","\xe3\x81","\x30\x81","\x30" "B"};
  if(command=="TEXTBAD")chunks={"A",std::string(1,char(0xff))};
  if(command=="TEXTPART")chunks={"A",std::string(1,char(0xe4))};
  if(command=="DELIM")chunks={"payloadab","ab","a","cTAILab","abac"};
  if(command=="GBDELIM")chunks={"payload\xc4","\xe3","TAIL\xc4","\xe3"};
  if(command=="BINARYDELIM")chunks={std::string("payload\0",8),"!TAIL",std::string("\0!",2)};
  for(size_t i=0;i<chunks.size();++i){auto timer=std::make_shared<asio::steady_timer>(io,std::chrono::milliseconds(i*5));auto bytes=std::make_shared<std::string>(chunks[i]);timer->async_wait([this,timer,bytes,destination](auto error){if(!error)udp.async_send_to(asio::buffer(*bytes),destination,[bytes](auto,size_t){});});}receive();});}
 Connection tcp(Driver& d,bool framed=true){Connection c;cd_tcp_options_t o;cd_tcp_options_init(&o);o.host=CD_SV("127.0.0.1");o.port=listener.local_endpoint().port();if(framed)o.defaults.receive_until=cd_receive_until_delimiter_string(CD_SV("\n"),0);Result e;ok(cd_create_tcp(d.handle,&o,&c.handle,&e.handle),e,"create tcp");c.open();return c;}
 Connection datagram(Driver& d){Connection c;cd_udp_options_t o;cd_udp_options_init(&o);o.remote_host=CD_SV("127.0.0.1");o.remote_port=udp.local_endpoint().port();o.defaults.receive_until=cd_receive_until_bytes(5);Result e;ok(cd_create_udp(d.handle,&o,&c.handle,&e.handle),e,"create udp");c.open();return c;}
};
#ifndef _WIN32
struct SerialPeer {
 asio::io_context io;std::unique_ptr<asio::posix::stream_descriptor> descriptor;std::thread thread;std::array<char,1024> buffer;std::string name;int slave=-1;
 SerialPeer(){int master=posix_openpt(O_RDWR|O_NOCTTY);if(master<0||grantpt(master)||unlockpt(master))throw std::runtime_error("PTY creation failed");name=ptsname(master);slave=::open(name.c_str(),O_RDWR|O_NOCTTY);termios settings{};tcgetattr(slave,&settings);cfmakeraw(&settings);tcsetattr(slave,TCSANOW,&settings);descriptor=std::make_unique<asio::posix::stream_descriptor>(io,master);read();thread=std::thread([this]{io.run();});}
 ~SerialPeer(){io.stop();thread.join();::close(slave);}
 void read(){descriptor->async_read_some(asio::buffer(buffer),[this](auto e,size_t n){if(e)return;auto bytes=std::make_shared<std::string>(buffer.data(),n);asio::async_write(*descriptor,asio::buffer(*bytes),[bytes](auto,size_t){});read();});}
 Connection connection(Driver& d){Connection c;cd_serial_options_t o;cd_serial_options_init(&o);o.port_name=sv(name);o.defaults.receive_until=cd_receive_until_bytes(5);Result e;ok(cd_create_serial(d.handle,&o,&c.handle,&e.handle),e,"create virtual serial");c.open();return c;}
};
#endif
static void core_tests(){
 Driver d;auto shell=cli(d);auto once=cli(d,true);
#ifdef _WIN32
 const std::string ready="echo|set /p=READY",no="echo|set /p=NO",state_set="set CD_TEST=STATE",state_get="echo|set /p=%CD_TEST%";
#else
 const std::string ready="printf READY",no="printf NO",state_set="cd /tmp; export CD_TEST=STATE",state_get="printf \"$CD_TEST\"";
#endif
 auto match=cd_check_contains_string(CD_SV("READY"));
 {Result r;execute(once,ready,r,&match);expect(r.text()=="READY"&&r.passed()&&r.info().completion_reason==CD_COMPLETION_COMMAND,"one-shot without newline");}
 {Result r;auto start=Clock::now();execute(shell,no,r,&match);if(r.text()!="NO")std::cerr<<"Unexpected managed reply ["<<r.text()<<"]\n";expect(!r.passed()&&r.text()=="NO","complete response mismatch is normal");expect(Clock::now()-start<std::chrono::seconds(1),"mismatch did not wait total timeout");}
 {Result r;execute(shell,state_set,r);Result next;execute(shell,state_get,next);expect(next.text()=="STATE","persistent environment retained");expect(next.text().find("__CD_")==std::string::npos,"internal markers stripped");}
#ifdef _WIN32
 {Result r;execute(shell,"echo READY",r);expect(r.text()=="READY\r\n","CMD business newline is preserved");}
#endif
#ifndef _WIN32
 {auto fifo=std::filesystem::temp_directory_path()/("cd-gate-"+std::to_string(Clock::now().time_since_epoch().count()));expect(mkfifo(fifo.c_str(),0600)==0,"create controlled command completion gate");
  struct RemoveGate {std::filesystem::path path;~RemoveGate(){std::error_code error;std::filesystem::remove(path,error);}} cleanup{fifo};
  std::string command="printf BUSY; printf READY; read CD_GATE < '"+fifo.string()+"'; printf TAIL";cd_call_options_t options;cd_call_options_init(&options);options.timeout_ms=2000;
  Result r;ok(cd_execute_until_string(shell.handle,sv(command),&match,&options,&r.handle),r,"until");expect(r.passed()&&r.info().completion_reason==CD_COMPLETION_MATCH,"incremental until match");expect(r.text().find("TAIL")==std::string::npos,"match returns while command is blocked on completion gate");
  {std::ofstream release(fifo);release<<"release\n";}Result next;execute(shell,ready,next);expect(next.text()=="READY","next command waits for cleanup and gets no TAIL");}
 {Result r;cd_call_options_t o;cd_call_options_init(&o);o.timeout_ms=40;expect(cd_execute_until_string(shell.handle,CD_SV("printf PART; sleep 0.2; printf LATE"),&match,&o,&r.handle)==CD_TIMEOUT,"timeout is operation error");expect(r.bytes().find("PART")!=std::string::npos,"timeout retains partial output");expect(cd_connection_get_state(shell.handle)==CD_STATE_NEEDS_RECOVERY,"timeout needs explicit recovery");Result next;expect(cd_execute_string(shell.handle,sv(ready),nullptr,nullptr,&next.handle)==CD_NEEDS_RECOVERY,"late output cannot enter next command");shell.close();shell.open();Result recovered;execute(shell,ready,recovered);expect(recovered.text()=="READY","close/open recovers safely");}
 {cd_operation_t* op=nullptr;auto input=std::string("sleep 0.03; printf READY");expect(cd_execute_string_async(shell.handle,sv(input),&match,nullptr,nullptr,nullptr,&op)==CD_OK,"submit async");input="mutated";Result observer;expect(cd_operation_wait(op,1,&observer.handle)==CD_TIMEOUT,"observer timeout");expect(!cd_operation_is_completed(op),"observer timeout did not cancel");Result r;wait(op,r);expect(r.text()=="READY","submission copies input");}
 {cd_operation_t* op=nullptr;expect(cd_execute_string_async(shell.handle,CD_SV("printf PART; sleep 1"),nullptr,nullptr,nullptr,nullptr,&op)==CD_OK,"cancel submit");cd_operation_cancel(op);Result r;wait(op,r);expect(r.status()==CD_CANCELLED,"operation cancellation");shell.close();shell.open();}
#endif
 {auto invalid=cd_check_regex(CD_SV("["));cd_operation_t* op=nullptr;expect(cd_execute_string_async(shell.handle,sv(ready),&invalid,nullptr,nullptr,nullptr,&op)==CD_INVALID_ARGUMENT&&!op,"invalid regex rejected before acceptance");}
 {auto eq=cd_check_equals_string(CD_SV("x"));Result r;expect(cd_execute_until_string(shell.handle,CD_SV("x"),&eq,nullptr,&r.handle)==CD_INVALID_ARGUMENT,"until rejects Equals");}
 {auto regex=cd_check_regex(CD_SV("(REA)(DY)"));Result r;ok(cd_response_checker_is_match_string(CD_SV("READY"),&regex,&r.handle),r,"checker");cd_check_result_t check{};check.struct_size=sizeof(check);cd_result_get_check(r.handle,&check);expect(check.passed&&check.group_count==2,"RE2 groups");}
 {struct Context {cd_connection_t* c;std::mutex mutex;std::condition_variable cv;bool called=false;int status=0;cd_result_t* kept=nullptr;} ctx;ctx.c=shell.handle;
  auto callback=[](cd_operation_t*,cd_result_t* result,void* user){auto& context=*static_cast<Context*>(user);Result close;context.status=cd_close(context.c,nullptr,&close.handle);cd_result_retain(result);context.kept=result;{std::lock_guard lock(context.mutex);context.called=true;}context.cv.notify_one();};
  cd_operation_t* op=nullptr;expect(cd_execute_string_async(shell.handle,sv(ready),nullptr,nullptr,callback,&ctx,&op)==CD_OK,"callback submit");cd_operation_release(op);std::unique_lock lock(ctx.mutex);expect(ctx.cv.wait_for(lock,std::chrono::seconds(5),[&]{return ctx.called;}),"callback exactly once despite released operation");expect(ctx.status==CD_INVALID_CONTEXT,"callback synchronous close refuses deadlock");cd_result_release(ctx.kept);
 }
 Peers peers;auto tcp=peers.tcp(d);auto unframed=peers.tcp(d,false);auto udp=peers.datagram(d);
 {Result r;int before=peers.requests;expect(cd_execute_string(unframed.handle,CD_SV("FRAME"),nullptr,nullptr,&r.handle)==CD_INVALID_ARGUMENT,"unframed execute rejected");expect(peers.requests==before,"configuration failure sends no bytes");}
 {Result r;execute(tcp,"FRAME",r,&match);expect(r.text()=="NOT"&&!r.passed(),"TCP framed mismatch returns normally");}
 {auto newline=cd_check_contains_string(CD_SV("\n"));Result r;expect(cd_execute_until_string(tcp.handle,CD_SV("FRAME"),&newline,nullptr,&r.handle)==CD_END_OF_STREAM&&r.text()=="NOT"&&!r.passed(),"until does not match a stripped protocol delimiter");}
 {auto ending=peers.tcp(d);Result r;expect(cd_execute_string(ending.handle,CD_SV("EOF"),nullptr,nullptr,&r.handle)==CD_END_OF_STREAM&&r.text()=="PART","incomplete protocol EOF preserves partial response");}
 {Result w;ok(cd_write_string(unframed.handle,CD_SV("UTF"),nullptr,&w.handle),w,"write UTF request");Result r;ok(cd_read_exactly_string(unframed.handle,2,nullptr,&r.handle),r,"read exact unicode scalars");expect(r.text()=="A\xf0\x9f\x98\x84","UTF8 split block and scalar count");}
 {Result w;ok(cd_write_string(unframed.handle,CD_SV("BAD"),nullptr,&w.handle),w,"write malformed UTF request");Result r;expect(cd_read_exactly_string(unframed.handle,2,nullptr,&r.handle)==CD_ENCODING_ERROR&&r.bytes()==std::string("A\xff",2)&&r.text()=="A","encoding error retains original partial bytes and valid text prefix");}
 {uint8_t value='A';auto bytes=cd_check_contains_bytes({&value,1});cd_call_options_t frame;cd_call_options_init(&frame);frame.receive_until=cd_receive_until_line(CD_SV("\n"));cd_operation_t* operation=nullptr;expect(cd_execute_until_string_async(unframed.handle,CD_SV("BAD"),&bytes,&frame,nullptr,nullptr,&operation)==CD_OK,"accept byte match with text response");Result result;wait(operation,result);expect(result.status()==CD_ENCODING_ERROR&&result.text()=="A","failed early text decoding still completes the accepted operation");unframed.close();unframed.open();}
 {cd_call_options_t overflow;cd_call_options_init(&overflow);overflow.timeout_ms=UINT64_MAX;Result r;expect(cd_read_string(unframed.handle,&overflow,&r.handle)==CD_INVALID_ARGUMENT,"oversized duration is rejected before IO");}
#ifndef _WIN32
 {auto isolated=cli(d,true);Result w;ok(cd_write_line(isolated.handle,CD_SV("printf RAW; sleep 0.1; printf LATE"),nullptr,&w.handle),w,"oneshot persistent stdio write");Result command;execute(isolated,"printf OUT",command);expect(command.text()=="OUT","CLI oneshot does not mix persistent output");Result raw;ok(cd_read_exactly_string(isolated.handle,7,nullptr,&raw.handle),raw,"isolated persistent read");expect(raw.text()=="RAWLATE","CLI persistent raw buffer retained across oneshot command");}
#endif
 {Result r;execute(udp,"UDP",r,&match);expect(r.text()=="READY"&&r.passed(),"READY spans UDP datagrams");}
 {cd_call_options_t small;cd_call_options_init(&small);small.max_response_bytes=5;Result r;execute(udp,"UDP",r,&match,&small);expect(r.text()=="READY","UDP response can exactly fill its byte limit across messages");}
 {Result w;ok(cd_write_bytes(udp.handle,{nullptr,0},nullptr,&w.handle),w,"write empty UDP datagram");auto message=cd_receive_until_message();Result r;ok(cd_read_until_bytes(udp.handle,&message,nullptr,&r.handle),r,"read empty native message");expect(r.bytes().empty(),"zero-length UDP message retains its boundary");}
 {auto contains=cd_check_contains_string(CD_SV(""));Result r;ok(cd_response_checker_is_match_string(CD_SV(""),&contains,&r.handle),r,"empty standalone check");cd_check_result_t check{};check.struct_size=sizeof(check);cd_result_get_check(r.handle,&check);expect(check.passed&&check.matched_text.data&&check.matched_text.size==0&&!check.matched_bytes.data,"empty matched text is applicable and present");}
 {Result w;ok(cd_write_string(udp.handle,CD_SV("UTF"),nullptr,&w.handle),w,"UDP UTF request");Result r;ok(cd_read_exactly_string(udp.handle,2,nullptr,&r.handle),r,"UDP exact scalars");expect(r.text()=="\xe4\xbd\xa0\xf0\x9f\x98\x84","UTF8 spans datagrams");}
 {Result w;ok(cd_write_string(udp.handle,CD_SV("GB"),nullptr,&w.handle),w,"UDP GB18030 request");cd_call_options_t encoding;cd_call_options_init(&encoding);encoding.encoding=CD_ENCODING_GB18030;Result r;ok(cd_read_exactly_string(udp.handle,2,&encoding,&r.handle),r,"GB18030 incremental scalars");expect(r.text()=="\xe4\xbd\xa0\xe5\xa5\xbd","GB18030 scalars span four datagrams");}
 {Result r;expect(cd_close_input(udp.handle,nullptr,&r.handle)==CD_NOT_SUPPORTED,"UDP input EOF unsupported");Result transfer;expect(cd_upload(tcp.handle,CD_SV("a"),CD_SV("b"),nullptr,&transfer.handle)==CD_NOT_SUPPORTED,"TCP upload unsupported");}
 {Result r;cd_call_options_t o;cd_call_options_init(&o);o.check_stream=CD_STREAM_STDERR;expect(cd_execute_string(tcp.handle,CD_SV("FRAME"),&match,&o,&r.handle)==CD_NOT_SUPPORTED,"stderr config refused before IO");}
 {Result r;cd_call_options_t o;cd_call_options_init(&o);o.receive_until=cd_receive_until_idle(25);execute(unframed,"LATE",r,nullptr,&o);expect(r.text()=="BUSY"&&r.info().completion_reason==CD_COMPLETION_IDLE,"explicit idle has documented long-gap truncation");Result next;expect(cd_connection_get_state(unframed.handle)==CD_STATE_NEEDS_RECOVERY&&cd_execute_string(unframed.handle,CD_SV("NEXT"),nullptr,&o,&next.handle)==CD_NEEDS_RECOVERY,"idle cannot silently assign delayed output to a new command");unframed.close();unframed.open();}
#ifndef _WIN32
 {Result r;cd_call_options_t o;cd_call_options_init(&o);o.max_response_bytes=64;expect(cd_execute_string(shell.handle,CD_SV("printf '%0100d' 0"),nullptr,&o,&r.handle)==CD_LIMIT_EXCEEDED,"buffer limit is explicit error");shell.close();shell.open();}
#endif
 {auto start=Clock::now();for(int i=0;i<50;++i){Result r;execute(shell,ready,r,&match);expect(r.passed(),"fast commands pass");}std::cout<<"METRIC 50 persistent shell commands ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-start).count()<<"\n";}
 {cd_call_options_t small;cd_call_options_init(&small);small.max_response_bytes=5;Result r;execute(shell,ready,r,&match,&small);expect(r.text()=="READY","managed control markers do not consume the reply byte limit");}
 {auto ending=cli(d);Result r;
#ifdef _WIN32
 execute(ending,"echo|set /p=OUT & exit 7",r);
#else
 execute(ending,"printf OUT; exit 7",r);
#endif
 expect(r.text().find("OUT")==0&&r.info().has_exit_code&&r.info().exit_code==7,"persistent shell exit reports drained output and real status");expect(cd_connection_get_state(ending.handle)==CD_STATE_FAULTED,"persistent shell exit is not silently restarted");ending.close();ending.open();Result recovered;execute(ending,ready,recovered);expect(recovered.text()=="READY","explicit reopen recreates an exited shell");}
#ifndef _WIN32
 SerialPeer serial;auto port=serial.connection(d);{Result r;execute(port,"READY",r,&match);expect(r.text()=="READY","virtual serial protocol");Result unsupported;expect(cd_close_input(port.handle,nullptr,&unsupported.handle)==CD_NOT_SUPPORTED,"serial close input unsupported");}
#endif
}
static void batch_semantics(){
#ifndef _WIN32
 Driver driver;auto shell=cli(driver);Peers peers;auto udp=peers.datagram(driver);auto match=cd_check_contains_string(CD_SV("READY"));
 cd_call_options_t short_timeout;cd_call_options_init(&short_timeout);short_timeout.timeout_ms=30;
 std::vector<cd_batch_item_t> items(3);for(size_t i=0;i<items.size();++i){items[i].struct_size=sizeof(cd_batch_item_t);items[i].kind=CD_BATCH_EXECUTE_STRING;items[i].key=i==0?CD_SV("first"):i==1?CD_SV("second"):CD_SV("third");items[i].connection=shell.handle;items[i].check=&match;items[i].command=CD_SV("printf READY");}
 items[0].command=CD_SV("sleep 0.1; printf READY");items[1].options=&short_timeout;
 {Result result;ok(cd_batch_run(driver.handle,items.data(),2,nullptr,&result.handle),result,"admitted batch");cd_batch_item_result_t second{};second.struct_size=sizeof(second);cd_result_get_batch_item(result.handle,1,&second);expect(second.state==CD_BATCH_TIMED_OUT,"timeout includes connection transaction queue");cd_batch_item_result_t first{};first.struct_size=sizeof(first);cd_result_get_batch_item(result.handle,0,&first);expect(first.succeeded,"single failure does not cancel other items");}
 {cd_batch_options_t options;cd_batch_options_init(&options);options.max_concurrency=1;Result result;ok(cd_batch_run(driver.handle,items.data(),2,&options,&result.handle),result,"batch scheduling timeout");cd_batch_info_t info{};info.struct_size=sizeof(info);cd_result_get_batch_info(result.handle,&info);expect(info.all_succeeded,"batch scheduling wait is outside individual timeout");}
 {cd_cancel_source_t* cancel=nullptr;expect(cd_cancel_source_create(&cancel)==CD_OK,"batch cancellation source");cd_batch_options_t options;cd_batch_options_init(&options);options.max_concurrency=1;options.cancellation=cancel;items[0].command=CD_SV("printf READY");items[1].command=CD_SV("printf PART; sleep 1");items[1].options=nullptr;auto timer=std::make_shared<asio::steady_timer>(peers.io,std::chrono::milliseconds(40));timer->async_wait([timer,cancel](auto e){if(!e)cd_cancel_source_request(cancel);});Result result;ok(cd_batch_run(driver.handle,items.data(),3,&options,&result.handle),result,"cancelled batch partial report");cd_batch_info_t info{};info.struct_size=sizeof(info);cd_result_get_batch_info(result.handle,&info);expect(info.was_cancelled&&!info.all_succeeded&&info.item_count==3,"cancellation returns complete indexed report");cd_batch_item_result_t first{},second{},third{};first.struct_size=second.struct_size=third.struct_size=sizeof(first);cd_result_get_batch_item(result.handle,0,&first);cd_result_get_batch_item(result.handle,1,&second);cd_result_get_batch_item(result.handle,2,&third);expect(first.succeeded&&second.state==CD_BATCH_CANCELLED&&third.state==CD_BATCH_NOT_STARTED,"completed, admitted and unscheduled items preserved");cd_cancel_source_release(cancel);shell.close();shell.open();}
#endif
}
static void mixed_batch(int seconds){
 Driver driver;Peers peers;
#ifndef _WIN32
 std::vector<std::unique_ptr<SerialPeer>> serials;
#endif
 std::vector<Connection> connections;std::vector<std::string> keys;connections.reserve(32);keys.reserve(32);
 for(int i=0;i<32;++i){if(i%4==0)connections.push_back(cli(driver));else if(i%4==1){auto c=peers.tcp(driver,false);connections.push_back(std::move(c));}else if(i%4==2)connections.push_back(peers.datagram(driver));else {
#ifndef _WIN32
 serials.push_back(std::make_unique<SerialPeer>());connections.push_back(serials.back()->connection(driver));
#else
 connections.push_back(cli(driver,true));
#endif
 }keys.push_back("UP"+std::to_string(i));}
 std::vector<cd_batch_item_t> items(32);auto check=cd_check_contains_string(CD_SV("READY"));cd_call_options_t options;cd_call_options_init(&options);options.receive_until=cd_receive_until_bytes(5);
 for(int i=0;i<32;++i){auto& b=items[i];b.struct_size=sizeof(b);b.kind=CD_BATCH_EXECUTE_STRING;b.key=sv(keys[i]);b.connection=connections[i].handle;b.check=&check;b.options=&options;
#ifdef _WIN32
 b.command=i%4==0||i%4==3?CD_SV("echo|set /p=READY"):CD_SV("READY");
#else
 b.command=i%4==0?CD_SV("printf READY"):CD_SV("READY");
#endif
 }
 auto begin=Clock::now();auto report=begin;uint64_t batches=0;do {Result result;ok(cd_batch_run(driver.handle,items.data(),items.size(),nullptr,&result.handle),result,"mixed batch");cd_batch_info_t info{};info.struct_size=sizeof(info);cd_result_get_batch_info(result.handle,&info);if(!info.all_succeeded){for(int i=0;i<32;++i){cd_batch_item_result_t r{};r.struct_size=sizeof(r);cd_result_get_batch_item(result.handle,i,&r);if(!r.succeeded){std::cerr<<keys[i]<<" status="<<cd_status_name(cd_result_get_status(r.result));for(int stream=0;stream<2;++stream){cd_bytes_view_t partial{};cd_result_get_bytes(r.result,stream,&partial);std::cerr<<" stream"<<stream<<"[";for(size_t j=0;j<partial.size&&j<1024;++j){auto c=partial.data[j];if(c=='\r')std::cerr<<"\\r";else if(c=='\n')std::cerr<<"\\n";else std::cerr<<char(c);}std::cerr<<"]";}std::cerr<<"\n";}}}expect(info.all_succeeded&&info.item_count==32,"32 mixed connections all pass");++batches;if(Clock::now()>=report){std::cout<<"SOAK batches="<<batches<<" elapsed_s="<<std::chrono::duration_cast<std::chrono::seconds>(Clock::now()-begin).count()<<std::endl;report=Clock::now()+std::chrono::seconds(10);}}while(Clock::now()-begin<std::chrono::seconds(seconds));
 std::cout<<"METRIC mixed_connections=32 batches="<<batches<<" operations="<<batches*32<<" elapsed_ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-begin).count()<<"\n";
}
static void incremental_text(){
 Driver driver;Peers peers;
 for(bool regex:{false,true}){
  auto c=peers.datagram(driver);cd_call_options_t o;cd_call_options_init(&o);o.receive_until=cd_receive_until_delimiter_string(CD_SV("\r\n"),0);
  auto check=regex?cd_check_regex(CD_SV("(\xe4\xbd\xa0)(\xf0\x9f\x98\x84)READY")):cd_check_contains_string(CD_SV("\xe4\xbd\xa0\xf0\x9f\x98\x84READY"));Result r;
  ok(cd_execute_until_string(c.handle,CD_SV("UTFMATCH"),&check,&o,&r.handle),r,"incremental Unicode match");expect(r.text()=="A\xe4\xbd\xa0\xf0\x9f\x98\x84READY"&&r.passed(),"match crosses UTF8 blocks and keeps prefix");
  if(regex){cd_check_result_t matched{};matched.struct_size=sizeof(matched);cd_result_get_check(r.handle,&matched);expect(matched.group_count==2,"incremental regex captures preserved");}
  Result tail;ok(cd_read_exactly_string(c.handle,4,nullptr,&tail.handle),tail,"tail after match cleanup");expect(tail.text()=="TAIL","decoder does not leak into next request");
 }
 {auto c=peers.datagram(driver);cd_call_options_t o;cd_call_options_init(&o);o.encoding=CD_ENCODING_GB18030;Result w;ok(cd_write_string(c.handle,CD_SV("GBSCALAR"),nullptr,&w.handle),w,"GB scalar request");Result r;ok(cd_read_exactly_string(c.handle,3,&o,&r.handle),r,"incremental GB scalars");expect(r.text()=="A\xe4\xbd\xa0\xc2\x80","GB two and four byte scalars cross blocks");Result tail;ok(cd_read_exactly_string(c.handle,1,&o,&tail.handle),tail,"GB scalar tail");expect(tail.text()=="B","exact scalar wire offset preserves tail");}
 {auto c=peers.datagram(driver);cd_call_options_t o;cd_call_options_init(&o);o.encoding=CD_ENCODING_GB18030;o.receive_until=cd_receive_until_delimiter_string(CD_SV("B"),0);auto check=cd_check_contains_string(CD_SV("\xe4\xbd\xa0\xc2\x80"));Result r;ok(cd_execute_until_string(c.handle,CD_SV("GBSCALAR"),&check,&o,&r.handle),r,"GB incremental check");expect(r.text()=="A\xe4\xbd\xa0\xc2\x80"&&r.passed(),"GB check and delimiter preserve wire offset");}
 {auto c=peers.datagram(driver);Result w;ok(cd_write_string(c.handle,CD_SV("UTFMATCH"),nullptr,&w.handle),w,"pattern request");auto rule=cd_receive_until_pattern(CD_SV("\xe4\xbd\xa0.*READY"));Result r;ok(cd_read_until_string(c.handle,&rule,nullptr,&r.handle),r,"incremental pattern boundary");expect(r.text()=="A\xe4\xbd\xa0\xf0\x9f\x98\x84READY","pattern keeps prefix and full regex context");Result tail;ok(cd_read_exactly_string(c.handle,6,nullptr,&tail.handle),tail,"pattern remaining bytes");expect(tail.text()=="\r\nTAIL","pattern boundary preserves remaining frame");}
#ifndef _WIN32
 {auto c=cli(driver,true);cd_call_options_t o;cd_call_options_init(&o);o.check_stream=CD_STREAM_STDERR;auto check=cd_check_contains_string(CD_SV("READY"));Result r;ok(cd_execute_until_string(c.handle,CD_SV("printf OUT; printf AREADY >&2"),&check,&o,&r.handle),r,"stderr incremental match");expect(r.passed()&&r.text(1)=="AREADY","check decoder uses selected stderr stream");}
#endif
 for(bool until:{false,true})for(bool malformed:{false,true}){
  auto c=peers.datagram(driver);cd_call_options_t o;cd_call_options_init(&o);o.timeout_ms=100;auto check=cd_check_contains_string(CD_SV("never"));Result r;
  cd_status_t status;if(until)status=cd_execute_until_string(c.handle,malformed?CD_SV("TEXTBAD"):CD_SV("TEXTPART"),&check,&o,&r.handle);
  else {Result w;ok(cd_write_string(c.handle,malformed?CD_SV("TEXTBAD"):CD_SV("TEXTPART"),nullptr,&w.handle),w,"partial text request");status=cd_read_exactly_string(c.handle,2,&o,&r.handle);}
  expect(status==(malformed?CD_ENCODING_ERROR:CD_TIMEOUT)&&r.text()=="A"&&r.bytes()==std::string("A")+char(malformed?0xff:0xe4),"incremental failure preserves full raw reply and valid prefix");
 }
}
static void batch_cancel_many(int count,bool close_race=false){
 Driver driver;Peers peers;auto c=peers.tcp(driver);auto control=cli(driver,true);
 struct Gate {std::mutex mutex;std::condition_variable cv;bool entered=false,released=false;
  void wait(){std::unique_lock lock(mutex);if(!cv.wait_for(lock,std::chrono::seconds(10),[&]{return entered;}))throw std::runtime_error("batch gate watchdog");}
  void release(){{std::lock_guard lock(mutex);released=true;}cv.notify_all();}
 } first,second;
 auto callback=[](cd_operation_t*,cd_result_t*,void* user){auto& g=*static_cast<Gate*>(user);std::unique_lock lock(g.mutex);g.entered=true;g.cv.notify_all();g.cv.wait(lock,[&]{return g.released;});};
 struct Release {Gate& a;Gate& b;~Release(){a.release();b.release();}} release{first,second};
 cd_operation_t* gate1=nullptr;expect(cd_open_async(control.handle,nullptr,callback,&first,&gate1)==CD_OK,"first gate accepted");first.wait();
 std::vector<cd_batch_item_t> items(count+3);std::vector<std::string> keys;keys.reserve(items.size());for(size_t i=0;i<items.size();++i)keys.push_back(std::to_string(i));
 for(size_t i=0;i<items.size();++i){auto& item=items[i];item.struct_size=sizeof(item);item.kind=CD_BATCH_EXECUTE_STRING;item.connection=c.handle;item.key=sv(keys[i]);item.command=CD_SV("WAIT");}
 cd_batch_options_t options;cd_batch_options_init(&options);options.max_concurrency=count;cd_operation_t* batch=nullptr;
 expect(cd_batch_run_async(driver.handle,items.data(),items.size(),&options,nullptr,nullptr,&batch)==CD_OK,"large batch accepted");
 cd_operation_t* gate2=nullptr;expect(cd_open_async(control.handle,nullptr,callback,&second,&gate2)==CD_OK,"second gate accepted");first.release();second.wait();
 auto begin=Clock::now();std::vector<cd_operation_t*> closing(close_race?32:0);std::vector<std::thread> threads;std::atomic<int> rejected{0};
 if(close_race){for(size_t i=0;i<closing.size();++i)threads.emplace_back([&,i]{cd_operation_cancel(batch);auto status=i%2?cd_close_async(c.handle,nullptr,nullptr,nullptr,&closing[i]):cd_driver_close_async(driver.handle,nullptr,nullptr,&closing[i]);if(status!=CD_OK)++rejected;});for(auto& thread:threads)thread.join();}
 else cd_operation_cancel(batch);
 second.release();Result r;wait(batch,r);
 auto us=std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-begin).count();cd_batch_info_t info{};info.struct_size=sizeof(info);cd_result_get_batch_info(r.handle,&info);expect(info.was_cancelled&&info.item_count==items.size(),"batch cancellation keeps complete report");
 for(int i=0;i<count+3;++i){cd_batch_item_result_t item{};item.struct_size=sizeof(item);cd_result_get_batch_item(r.handle,i,&item);expect(item.state==(i<count?CD_BATCH_CANCELLED:CD_BATCH_NOT_STARTED)||(close_race&&i<count&&item.state==CD_BATCH_FAILED&&cd_result_get_status(item.result)==CD_CLOSED),"admitted cancellation and unstarted items preserved");}
 for(auto operation:closing){Result closed;wait(operation,closed);expect(closed.status()==CD_OK,"concurrent batch close succeeds");}expect(!rejected,"all batch closes accepted");
 Result a,b;wait(gate1,a);wait(gate2,b);std::cout<<"METRIC batch_cancel count="<<count<<" close_race="<<close_race<<" elapsed_us="<<us<<"\n";
}
static void delimiter_fragments(){
 Driver driver;Peers peers;
 for(int mode=0;mode<3;++mode)for(bool include:{false,true}){
  auto connection=peers.datagram(driver);cd_call_options_t options;cd_call_options_init(&options);options.receive_until=cd_receive_until_delimiter_string(CD_SV("ababac"),include);
  Result result;
  if(mode==0){Result written;ok(cd_write_string(connection.handle,CD_SV("DELIM"),nullptr,&written.handle),written,"write fragmented delimiter request");ok(cd_read_until_string(connection.handle,&options.receive_until,&options,&result.handle),result,"read fragmented delimiter");}
  else if(mode==1)execute(connection,"DELIM",result,nullptr,&options);
  else {auto check=cd_check_contains_string(CD_SV("payload"));ok(cd_execute_until_string(connection.handle,CD_SV("DELIM"),&check,&options,&result.handle),result,"early match before split delimiter");expect(result.passed(),"early match passes");}
  expect(result.text()==(include?(mode==2?"payloadab":"payloadababac"):"payload"),"overlapping delimiter spans multiple datagrams");
  Result tail;ok(cd_read_until_string(connection.handle,&options.receive_until,&options,&tail.handle),tail,"read frame after split delimiter");expect(tail.text()==(include?"TAILababac":"TAIL"),"next request resets search and early cleanup preserves tail");
 }
 {auto connection=peers.datagram(driver);cd_call_options_t options;cd_call_options_init(&options);options.encoding=CD_ENCODING_GB18030;options.receive_until=cd_receive_until_delimiter_string(CD_SV("\xe4\xbd\xa0"),0);
  Result result;execute(connection,"GBDELIM",result,nullptr,&options);expect(result.text()=="payload","text delimiter uses selected wire encoding");Result tail;ok(cd_read_until_string(connection.handle,&options.receive_until,&options,&tail.handle),tail,"encoded delimiter next frame");expect(tail.text()=="TAIL","encoded delimiter spans datagrams");}
 {auto connection=peers.datagram(driver);uint8_t delimiter[]={0,'!'};auto rule=cd_receive_until_delimiter_bytes({delimiter,2},0);Result written;ok(cd_write_string(connection.handle,CD_SV("BINARYDELIM"),nullptr,&written.handle),written,"binary delimiter request");Result result;ok(cd_read_until_bytes(connection.handle,&rule,nullptr,&result.handle),result,"binary delimiter read");expect(result.bytes()=="payload","NUL delimiter matches across datagrams");Result tail;ok(cd_read_until_bytes(connection.handle,&rule,nullptr,&tail.handle),tail,"binary next frame");expect(tail.bytes()=="TAIL","binary next frame intact");}
}
static void delimiter_bulk(size_t mebibytes){
 Driver driver;Peers peers;auto connection=peers.tcp(driver,false);size_t size=mebibytes*1024*1024;
 cd_call_options_t options;cd_call_options_init(&options);options.max_response_bytes=size+64;options.timeout_ms=30000;options.receive_until=cd_receive_until_delimiter_string(CD_SV("\r\n"),0);
 auto begin=Clock::now();Result result;execute(connection,"BULK:"+std::to_string(size),result,nullptr,&options);
 auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-begin).count();
 expect(result.text()==std::string(size,'x'),"fragmented large delimiter response is intact");
 Result tail;ok(cd_read_until_string(connection.handle,&options.receive_until,&options,&tail.handle),tail,"read next buffered delimiter frame");expect(tail.text()=="TAIL","delimiter search state resets for next request");
 std::cout<<"METRIC delimiter_mebibytes="<<mebibytes<<" elapsed_us="<<elapsed<<"\n";
}
static void text_work(size_t mebibytes,int mode){
 Driver driver;Peers peers;auto connection=peers.tcp(driver,false);size_t size=mebibytes*1024*1024;
 cd_call_options_t options;cd_call_options_init(&options);options.max_response_bytes=size+64;options.timeout_ms=60000;options.receive_until=cd_receive_until_delimiter_string(CD_SV("\r\n"),0);
 std::string command="BULK:"+std::to_string(size);Result result;auto begin=Clock::now();
 if(mode==0)execute(connection,command,result,nullptr,&options);
 else if(mode==3){Result written;ok(cd_write_string(connection.handle,sv(command),&options,&written.handle),written,"write");ok(cd_read_exactly_string(connection.handle,size,&options,&result.handle),result,"scalar read");}
 else {uint8_t absent[]={'z'};auto check=mode==1?cd_check_contains_string(CD_SV("z")):cd_check_contains_bytes({absent,1});expect(cd_execute_until_string(connection.handle,sv(command),&check,&options,&result.handle)==CD_END_OF_STREAM,"nonmatching response boundary");}
 auto us=std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-begin).count();expect(result.text()==std::string(size,'x'),"intact reply");std::cout<<"text mebibytes="<<mebibytes<<" mode="<<mode<<" us="<<us<<std::endl;
}
static void callback_release_race(){
 struct Context {std::mutex mutex;std::condition_variable cv;int completed=0;bool noninline=true;std::thread::id caller=std::this_thread::get_id();std::vector<cd_result_t*> retained;} context;
 auto callback=[](cd_operation_t*,cd_result_t* result,void* user){auto& c=*static_cast<Context*>(user);cd_result_retain(result);{std::lock_guard lock(c.mutex);c.retained.push_back(result);++c.completed;c.noninline=c.noninline&&std::this_thread::get_id()!=c.caller;}c.cv.notify_one();};
 {Driver driver;auto shell=cli(driver);for(int i=0;i<100;++i){cd_operation_t* operation=nullptr;
#ifdef _WIN32
 auto input=CD_SV("echo|set /p=READY");
#else
 auto input=CD_SV("printf READY");
#endif
 expect(cd_execute_string_async(shell.handle,input,nullptr,nullptr,callback,&context,&operation)==CD_OK,"accept operation before concurrent handle release");if(i%2==0)cd_operation_cancel(operation);cd_operation_release(operation);}}
 std::unique_lock lock(context.mutex);expect(context.cv.wait_for(lock,std::chrono::seconds(10),[&]{return context.completed==100;}),"accepted callbacks complete despite driver, connection and operation release");expect(context.noninline&&context.retained.size()==100,"callbacks are non-inline and exactly once");for(auto result:context.retained){expect(cd_result_get_status(result)!=CD_INTERNAL_ERROR,"retained callback result remains valid");cd_result_release(result);}
}
int main(int argc,char** argv){try {if(argc==3&&std::string(argv[1])=="--soak-seconds")mixed_batch(std::stoi(argv[2]));else if(argc==3&&std::string(argv[1])=="--batch-close-rounds"){for(int i=0;i<std::stoi(argv[2]);++i)batch_cancel_many(1024,true);}else if(argc==3&&std::string(argv[1])=="--delimiter-mebibytes")delimiter_bulk(std::stoull(argv[2]));else if(argc==3&&std::string(argv[1])=="--batch-cancel-count")batch_cancel_many(std::stoi(argv[2]));else if(argc==2&&std::string(argv[1])=="--text-performance"){for(size_t size:{1,2,4,8})for(int mode=0;mode<4;++mode)text_work(size,mode);}else {incremental_text();batch_cancel_many(128);batch_cancel_many(128,true);delimiter_fragments();delimiter_bulk(1);core_tests();batch_semantics();mixed_batch(1);callback_release_race();}std::cout<<"PASS checks="<<checks<<"\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
