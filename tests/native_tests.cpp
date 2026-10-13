#include <communicationdriver/communicationdriver.h>
#include <boost/asio.hpp>
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
   else if(command=="BAD")self->send_chunks({std::string("A\xff",2)});
   else if(command=="LATE")self->send_chunks({"BUSY","READY"},150);
   else if(command=="FRAME")self->send_chunks({"NOT","\n"});
   else self->send_chunks({command});self->read();});}
  void send_chunks(std::vector<std::string> data,int delay=5){auto self=shared_from_this();for(size_t i=0;i<data.size();++i){auto timer=std::make_shared<asio::steady_timer>(owner.io,std::chrono::milliseconds(i*delay));auto bytes=std::make_shared<std::string>(data[i]);timer->async_wait([self,timer,bytes](auto e){if(!e)asio::async_write(self->socket,asio::buffer(*bytes),[self,bytes](auto,size_t){});});}}
 };
 std::array<char,65536> udp_buffer;asio::ip::udp::endpoint sender;
 Peers(){accept();receive();thread=std::thread([this]{io.run();});}
 ~Peers(){io.stop();thread.join();}
 void accept(){listener.async_accept([this](auto e,auto socket){if(!e)std::make_shared<Session>(*this,std::move(socket))->read();accept();});}
 void receive(){udp.async_receive_from(asio::buffer(udp_buffer),sender,[this](auto e,size_t n){if(e)return;std::string command(udp_buffer.data(),n);auto destination=sender;++requests;
  std::vector<std::string> chunks=command=="UDP"?std::vector<std::string>{"REA","DY"}:command=="UTF"?std::vector<std::string>{"\xe4","\xbd","\xa0\xf0\x9f","\x98\x84"}:std::vector<std::string>{command};
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
 {Result w;ok(cd_write_string(unframed.handle,CD_SV("UTF"),nullptr,&w.handle),w,"write UTF request");Result r;ok(cd_read_exactly_string(unframed.handle,2,nullptr,&r.handle),r,"read exact unicode scalars");expect(r.text()=="A\xf0\x9f\x98\x84","UTF8 split block and scalar count");}
 {Result w;ok(cd_write_string(unframed.handle,CD_SV("BAD"),nullptr,&w.handle),w,"write malformed UTF request");Result r;expect(cd_read_exactly_string(unframed.handle,2,nullptr,&r.handle)==CD_ENCODING_ERROR&&r.bytes()==std::string("A\xff",2),"encoding error retains original partial bytes");}
 {cd_call_options_t overflow;cd_call_options_init(&overflow);overflow.timeout_ms=UINT64_MAX;Result r;expect(cd_read_string(unframed.handle,&overflow,&r.handle)==CD_INVALID_ARGUMENT,"oversized duration is rejected before IO");}
#ifndef _WIN32
 {auto isolated=cli(d,true);Result w;ok(cd_write_line(isolated.handle,CD_SV("printf RAW; sleep 0.1; printf LATE"),nullptr,&w.handle),w,"oneshot persistent stdio write");Result command;execute(isolated,"printf OUT",command);expect(command.text()=="OUT","CLI oneshot does not mix persistent output");Result raw;ok(cd_read_exactly_string(isolated.handle,7,nullptr,&raw.handle),raw,"isolated persistent read");expect(raw.text()=="RAWLATE","CLI persistent raw buffer retained across oneshot command");}
#endif
 {Result r;execute(udp,"UDP",r,&match);expect(r.text()=="READY"&&r.passed(),"READY spans UDP datagrams");}
 {Result w;ok(cd_write_string(udp.handle,CD_SV("UTF"),nullptr,&w.handle),w,"UDP UTF request");Result r;ok(cd_read_exactly_string(udp.handle,2,nullptr,&r.handle),r,"UDP exact scalars");expect(r.text()=="\xe4\xbd\xa0\xf0\x9f\x98\x84","UTF8 spans datagrams");}
 {Result r;expect(cd_close_input(udp.handle,nullptr,&r.handle)==CD_NOT_SUPPORTED,"UDP input EOF unsupported");Result transfer;expect(cd_upload(tcp.handle,CD_SV("a"),CD_SV("b"),nullptr,&transfer.handle)==CD_NOT_SUPPORTED,"TCP upload unsupported");}
 {Result r;cd_call_options_t o;cd_call_options_init(&o);o.check_stream=CD_STREAM_STDERR;expect(cd_execute_string(tcp.handle,CD_SV("FRAME"),&match,&o,&r.handle)==CD_NOT_SUPPORTED,"stderr config refused before IO");}
 {Result r;cd_call_options_t o;cd_call_options_init(&o);o.receive_until=cd_receive_until_idle(25);execute(unframed,"LATE",r,nullptr,&o);expect(r.text()=="BUSY"&&r.info().completion_reason==CD_COMPLETION_IDLE,"explicit idle has documented long-gap truncation");unframed.close();unframed.open();}
#ifndef _WIN32
 {Result r;cd_call_options_t o;cd_call_options_init(&o);o.max_response_bytes=64;expect(cd_execute_string(shell.handle,CD_SV("printf '%0100d' 0"),nullptr,&o,&r.handle)==CD_LIMIT_EXCEEDED,"buffer limit is explicit error");shell.close();shell.open();}
#endif
 {auto start=Clock::now();for(int i=0;i<50;++i){Result r;execute(shell,ready,r,&match);expect(r.passed(),"fast commands pass");}std::cout<<"METRIC 50 persistent shell commands ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-start).count()<<"\n";}
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
 auto begin=Clock::now();uint64_t batches=0;do {Result result;ok(cd_batch_run(driver.handle,items.data(),items.size(),nullptr,&result.handle),result,"mixed batch");cd_batch_info_t info{};info.struct_size=sizeof(info);cd_result_get_batch_info(result.handle,&info);if(!info.all_succeeded){for(int i=0;i<32;++i){cd_batch_item_result_t r{};r.struct_size=sizeof(r);cd_result_get_batch_item(result.handle,i,&r);if(!r.succeeded){std::cerr<<keys[i]<<" status="<<cd_status_name(cd_result_get_status(r.result));for(int stream=0;stream<2;++stream){cd_bytes_view_t partial{};cd_result_get_bytes(r.result,stream,&partial);std::cerr<<" stream"<<stream<<"[";for(size_t j=0;j<partial.size&&j<1024;++j){auto c=partial.data[j];if(c=='\r')std::cerr<<"\\r";else if(c=='\n')std::cerr<<"\\n";else std::cerr<<char(c);}std::cerr<<"]";}std::cerr<<"\n";}}}expect(info.all_succeeded&&info.item_count==32,"32 mixed connections all pass");++batches;if(batches%1000==0)std::cout<<"SOAK batches="<<batches<<" elapsed_s="<<std::chrono::duration_cast<std::chrono::seconds>(Clock::now()-begin).count()<<std::endl;}while(Clock::now()-begin<std::chrono::seconds(seconds));
 std::cout<<"METRIC mixed_connections=32 batches="<<batches<<" operations="<<batches*32<<" elapsed_ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-begin).count()<<"\n";
}
int main(int argc,char** argv){try {if(argc==3&&std::string(argv[1])=="--soak-seconds")mixed_batch(std::stoi(argv[2]));else {core_tests();batch_semantics();mixed_batch(1);}std::cout<<"PASS checks="<<checks<<"\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
