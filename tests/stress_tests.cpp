#include <communicationdriver/communicationdriver.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using Clock=std::chrono::steady_clock;
static void expect(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class T,void(*Release)(T*)> using Handle=std::unique_ptr<T,decltype(Release)>;
using Driver=Handle<cd_driver_t,cd_driver_release>;
using Connection=Handle<cd_connection_t,cd_connection_release>;
using Operation=Handle<cd_operation_t,cd_operation_release>;
using Result=Handle<cd_result_t,cd_result_release>;
static Driver driver(){cd_driver_t* h=nullptr;expect(cd_driver_create(&h)==CD_OK,"create driver");return Driver(h,cd_driver_release);}
static Connection cli(Driver& d,bool one_shot=true){cd_cli_options_t options;cd_cli_options_init(&options);options.execution_mode=one_shot?CD_ONE_SHOT:CD_PERSISTENT;cd_connection_t* h=nullptr;cd_result_t* error=nullptr;auto status=cd_create_cli(d.get(),&options,&h,&error);Result result(error,cd_result_release);expect(status==CD_OK,"create CLI");return Connection(h,cd_connection_release);}
static Result wait(cd_operation_t* operation){cd_result_t* h=nullptr;expect(cd_operation_wait(operation,5000,&h)==CD_OK,"accepted operation must complete within watchdog");return Result(h,cd_result_release);}
static void open(Connection& c){cd_result_t* h=nullptr;auto status=cd_open(c.get(),nullptr,&h);Result result(h,cd_result_release);expect(status==CD_OK,"open CLI");}
static void close(Connection& c){cd_result_t* h=nullptr;auto status=cd_close(c.get(),nullptr,&h);Result result(h,cd_result_release);expect(status==CD_OK,"close CLI");}

// Hold the IO worker until every Close is queued. No scheduler timing assumptions.
static void simultaneous_close(){
 struct Gate {std::mutex mutex;std::condition_variable cv;bool entered=false,released=false;std::atomic<int> callbacks{0};} gate;
 auto d=driver();auto c=cli(d);
 auto opened=[](cd_operation_t*,cd_result_t*,void* user){auto& g=*static_cast<Gate*>(user);std::unique_lock lock(g.mutex);g.entered=true;g.cv.notify_all();g.cv.wait(lock,[&]{return g.released;});};
 auto closed=[](cd_operation_t*,cd_result_t*,void* user){++static_cast<Gate*>(user)->callbacks;};
 cd_operation_t* initial=nullptr;expect(cd_open_async(c.get(),nullptr,opened,&gate,&initial)==CD_OK,"submit open gate");Operation opening(initial,cd_operation_release);
 {std::unique_lock lock(gate.mutex);expect(gate.cv.wait_for(lock,std::chrono::seconds(5),[&]{return gate.entered;}),"IO worker enters gate");}
 std::vector<Operation> operations;
 for(int i=0;i<128;++i){cd_operation_t* op=nullptr;expect(cd_close_async(c.get(),nullptr,closed,&gate,&op)==CD_OK,"accept simultaneous close");operations.emplace_back(op,cd_operation_release);}
 {std::lock_guard lock(gate.mutex);gate.released=true;}gate.cv.notify_all();
 for(auto& op:operations){auto result=wait(op.get());expect(cd_result_get_status(result.get())==CD_OK,"every close succeeds");}
 // Operation::wait observes the result before its callback; this barrier drains callbacks.
 close(c);expect(gate.callbacks==128,"every close callback occurs exactly once");
 open(c);close(c);std::cout<<"PASS simultaneous_close operations=128\n";
}

static void cancellation_storm(){
 auto d=driver();auto c=cli(d,false);open(c);
 cd_cancel_source_t* raw=nullptr;expect(cd_cancel_source_create(&raw)==CD_OK,"create shared cancellation source");
 Handle<cd_cancel_source_t,cd_cancel_source_release> cancel(raw,cd_cancel_source_release);
 std::atomic<int> failures{0};
 auto callback=[](cd_operation_t*,cd_result_t* result,void* user){auto* counts=static_cast<std::atomic<int>*>(user);if(cd_result_get_status(result)==CD_INTERNAL_ERROR)++counts[1];++counts[0];};
 std::atomic<int> counts[2]{};std::vector<std::thread> producers;
 for(int thread=0;thread<8;++thread)producers.emplace_back([&]{for(int i=0;i<500;++i){
  cd_call_options_t options;cd_call_options_init(&options);options.cancellation=cancel.get();
  cd_operation_t* op=nullptr;
#ifdef _WIN32
  auto command=CD_SV("echo|set /p=READY");
#else
  auto command=CD_SV("printf READY");
#endif
  if(cd_execute_string_async(c.get(),command,nullptr,&options,callback,counts,&op)!=CD_OK){++failures;continue;}
  if(i%2==0)cd_operation_cancel(op);cd_operation_release(op);
  if(i==100)cd_cancel_source_request(cancel.get());
 }});
 for(auto& thread:producers)thread.join();close(c);
 expect(!failures&&!counts[1]&&counts[0]==4000,"cancel storm completes every released operation exactly once");
 open(c);close(c);std::cout<<"PASS cancellation_storm producers=8 operations=4000\n";
}

static void duration_limits(){
 auto d=driver();auto c=cli(d);open(c);
 for(uint64_t value:{uint64_t(INT64_MAX),UINT64_MAX,uint64_t(10000000000000)}){
  cd_call_options_t options;cd_call_options_init(&options);options.timeout_ms=value;
  cd_operation_t* op=nullptr;expect(cd_open_async(c.get(),&options,nullptr,nullptr,&op)==CD_INVALID_ARGUMENT&&!op,"reject unsafe call duration before acceptance");
  cd_transfer_options_t transfer;cd_transfer_options_init(&transfer);transfer.timeout_ms=value;
  expect(cd_upload_async(c.get(),CD_SV("source"),CD_SV("target"),&transfer,nullptr,nullptr,&op)==CD_INVALID_ARGUMENT&&!op,"reject unsafe transfer duration");
  auto rule=cd_receive_until_idle(value);expect(cd_read_until_bytes_async(c.get(),&rule,nullptr,nullptr,nullptr,&op)==CD_INVALID_ARGUMENT&&!op,"reject unsafe idle duration");
  cd_cli_options_t defaults;cd_cli_options_init(&defaults);defaults.defaults.connect_timeout_ms=value;cd_connection_t* h=nullptr;cd_result_t* error=nullptr;
  expect(cd_create_cli(d.get(),&defaults,&h,&error)==CD_INVALID_ARGUMENT&&!h,"reject unsafe default duration");cd_result_release(error);
 }
 cd_operation_t* raw=nullptr;expect(cd_open_async(c.get(),nullptr,nullptr,nullptr,&raw)==CD_OK,"accept ordinary duration");Operation op(raw,cd_operation_release);
 cd_result_t* result=nullptr;expect(cd_operation_wait(op.get(),INT64_MAX,&result)==CD_INVALID_ARGUMENT&&cd_result_get_status(result)==CD_INVALID_ARGUMENT,"reject unsafe observer duration");cd_result_release(result);
 auto completed=wait(op.get());close(c);std::cout<<"PASS duration_limits\n";
}
static void lifecycle_churn(int rounds){
 auto begin=Clock::now();
 for(int i=0;i<rounds;++i){auto d=driver();auto c=cli(d,false);open(c);close(c);open(c);close(c);}
 std::cout<<"METRIC lifecycle_drivers="<<rounds<<" process_launches="<<rounds*2<<" elapsed_ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-begin).count()<<"\n";
}
static void retained_operations(int count){
 auto d=driver();auto c=cli(d);cd_cancel_source_t* raw=nullptr;expect(cd_cancel_source_create(&raw)==CD_OK,"create reusable token");
 Handle<cd_cancel_source_t,cd_cancel_source_release> source(raw,cd_cancel_source_release);
 cd_call_options_t options;cd_call_options_init(&options);options.cancellation=source.get();
 std::vector<Operation> operations;auto begin=Clock::now();
 for(int i=0;i<count;++i){cd_operation_t* op=nullptr;expect(cd_read_bytes_async(c.get(),&options,nullptr,nullptr,&op)==CD_OK,"accept retained operation");operations.emplace_back(op,cd_operation_release);auto r=wait(op);expect(cd_result_get_status(r.get())==CD_NOT_OPEN,"complete retained operation");}
 std::cout<<"METRIC retained_operations="<<count<<" elapsed_ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-begin).count()<<"\n";
 cd_cancel_source_request(source.get());for(auto& op:operations)expect(cd_operation_is_completed(op.get()),"retained results survive token cancellation");
}
int main(int argc,char** argv){try{if(argc==3&&std::string(argv[1])=="--retained")retained_operations(std::stoi(argv[2]));else{simultaneous_close();cancellation_storm();duration_limits();retained_operations(1000);lifecycle_churn(argc==2?std::stoi(argv[1]):100);}std::cout<<"PASS native stress\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
