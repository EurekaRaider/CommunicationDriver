#include <communicationdriver/communicationdriver.h>
#include <atomic>
#include <barrier>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#ifdef __GLIBC__
#include <malloc.h>
#endif
#ifdef __APPLE__
#if !__has_feature(address_sanitizer) && !__has_feature(thread_sanitizer)
#define CD_MALLOC_STATISTICS
#include <malloc/malloc.h>
#endif
#endif

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
static void pending_subscriptions(int count,bool shared,bool cancel=false){
 struct Gate {std::mutex mutex;std::condition_variable cv;bool entered=false,released=false;} gate;
 auto d=driver();auto gate_connection=cli(d);auto c=cli(d);
 auto callback=[](cd_operation_t*,cd_result_t*,void* user){auto& g=*static_cast<Gate*>(user);std::unique_lock lock(g.mutex);g.entered=true;g.cv.notify_all();g.cv.wait(lock,[&]{return g.released;});};
 cd_operation_t* first=nullptr;expect(cd_open_async(gate_connection.get(),nullptr,callback,&gate,&first)==CD_OK,"gate accepted");Operation opening(first,cd_operation_release);
 {std::unique_lock lock(gate.mutex);gate.cv.wait(lock,[&]{return gate.entered;});}
 cd_cancel_source_t* raw=nullptr;expect(cd_cancel_source_create(&raw)==CD_OK,"source");Handle<cd_cancel_source_t,cd_cancel_source_release> source(raw,cd_cancel_source_release);
 cd_call_options_t options;cd_call_options_init(&options);options.timeout_ms=60000;if(shared)options.cancellation=source.get();
 struct ReleaseGate {Gate& gate;~ReleaseGate(){{std::lock_guard lock(gate.mutex);gate.released=true;}gate.cv.notify_all();}} release{gate};
 std::vector<Operation> operations;operations.reserve(count);auto begin=Clock::now();
 for(int i=0;i<count;++i){cd_operation_t* op=nullptr;expect(cd_read_bytes_async(c.get(),&options,nullptr,nullptr,&op)==CD_OK,"submit");operations.emplace_back(op,cd_operation_release);}
 if(cancel)cd_cancel_source_request(source.get());
 auto us=std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-begin).count();
 {std::lock_guard lock(gate.mutex);gate.released=true;}gate.cv.notify_all();
 for(auto& op:operations){auto result=wait(op.get());expect(cd_result_get_status(result.get())==(cancel?CD_CANCELLED:CD_NOT_OPEN),"drained");}
 std::cout<<"pending shared="<<shared<<" count="<<count<<" us="<<us<<"\n";
}
static void registry(int count){auto d=driver();std::vector<Connection> connections;connections.reserve(count);auto begin=Clock::now();for(int i=0;i<count;++i)connections.push_back(cli(d));auto us=std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-begin).count();cd_result_t* raw=nullptr;expect(cd_driver_close(d.get(),&raw)==CD_OK,"driver close");Result result(raw,cd_result_release);for(auto& c:connections)expect(cd_connection_get_state(c.get())==CD_STATE_CLOSED,"driver closes every registered connection");std::cout<<"registry count="<<count<<" us="<<us<<"\n";}
static void precancelled_queued(){
 auto d=driver();auto c=cli(d,false);auto barrier=cli(d);open(c);
 cd_call_options_t options;cd_call_options_init(&options);options.timeout_ms=60000;
 cd_operation_t* raw=nullptr;expect(cd_read_bytes_async(c.get(),&options,nullptr,nullptr,&raw)==CD_OK,"blocked read accepted");Operation head(raw,cd_operation_release);open(barrier);
 cd_cancel_source_t* token=nullptr;expect(cd_cancel_source_create(&token)==CD_OK,"pre-cancel source");Handle<cd_cancel_source_t,cd_cancel_source_release> source(token,cd_cancel_source_release);cd_cancel_source_request(token);options.cancellation=token;
 expect(cd_read_bytes_async(c.get(),&options,nullptr,nullptr,&raw)==CD_OK,"pre-cancelled queued read accepted");Operation cancelled(raw,cd_operation_release);open(barrier);
 bool prompt=cd_operation_is_completed(cancelled.get());bool head_pending=!cd_operation_is_completed(head.get());close(c);
 expect(prompt&&head_pending,"pre-cancelled queued request completes without waiting for active read");auto result=wait(cancelled.get());expect(cd_result_get_status(result.get())==CD_CANCELLED,"pre-cancelled queued status");
 std::cout<<"PASS precancelled_queued\n";
}
static void expired_before_admission(){
 struct Gate {std::mutex mutex;std::condition_variable cv;bool entered=false,released=false;} gate;
 auto d=driver();auto barrier=cli(d);auto c=cli(d);
 auto block=[](cd_operation_t*,cd_result_t*,void* user){auto& g=*static_cast<Gate*>(user);std::unique_lock lock(g.mutex);g.entered=true;g.cv.notify_all();g.cv.wait(lock,[&]{return g.released;});};
 cd_operation_t* raw=nullptr;expect(cd_open_async(barrier.get(),nullptr,block,&gate,&raw)==CD_OK,"deadline gate");Operation first(raw,cd_operation_release);
 struct Release {Gate& g;~Release(){{std::lock_guard lock(g.mutex);g.released=true;}g.cv.notify_all();}} release{gate};
 {std::unique_lock lock(gate.mutex);gate.cv.wait(lock,[&]{return gate.entered;});}
 cd_call_options_t options;cd_call_options_init(&options);options.timeout_ms=1;
 expect(cd_open_async(c.get(),&options,nullptr,nullptr,&raw)==CD_OK,"short deadline accepted");Operation opening(raw,cd_operation_release);
 // Keep the worker blocked until the request's deadline has definitely passed.
 std::this_thread::sleep_until(Clock::now()+std::chrono::milliseconds(5));
 {std::lock_guard lock(gate.mutex);gate.released=true;}gate.cv.notify_all();
 auto result=wait(opening.get());expect(cd_result_get_status(result.get())==CD_TIMEOUT,"expired request must not start after event-loop admission delay");expect(cd_connection_get_state(c.get())==CD_STATE_CREATED,"expired open performs no backend work");
 std::cout<<"PASS expired_before_admission\n";
}
static void cancelled_batch_snapshot(){
 struct Gate {std::mutex mutex;std::condition_variable cv;bool entered=false,released=false;} gate;
 auto d=driver();auto c=cli(d);
 auto block=[](cd_operation_t*,cd_result_t*,void* user){auto& g=*static_cast<Gate*>(user);std::unique_lock lock(g.mutex);g.entered=true;g.cv.notify_all();g.cv.wait(lock,[&]{return g.released;});};
 cd_operation_t* raw=nullptr;expect(cd_open_async(c.get(),nullptr,block,&gate,&raw)==CD_OK,"snapshot gate");Operation first(raw,cd_operation_release);
 struct Release {Gate& g;~Release(){{std::lock_guard lock(g.mutex);g.released=true;}g.cv.notify_all();}} release{gate};
 {std::unique_lock lock(gate.mutex);gate.cv.wait(lock,[&]{return gate.entered;});}
 cd_cancel_source_t* token=nullptr;expect(cd_cancel_source_create(&token)==CD_OK,"batch token");Handle<cd_cancel_source_t,cd_cancel_source_release> source(token,cd_cancel_source_release);cd_cancel_source_request(token);
 struct Snapshot {uint64_t elapsed=0;int callbacks=0;} snapshot;
 auto complete=[](cd_operation_t*,cd_result_t* result,void* user){auto& s=*static_cast<Snapshot*>(user);cd_batch_info_t info{};info.struct_size=sizeof(info);cd_result_get_batch_info(result,&info);s.elapsed=info.elapsed_ns;++s.callbacks;};
 cd_batch_options_t options;cd_batch_options_init(&options);options.cancellation=token;
 expect(cd_batch_run_async(d.get(),nullptr,0,&options,complete,&snapshot,&raw)==CD_OK,"pre-cancelled empty batch");Operation batch(raw,cd_operation_release);
 {std::lock_guard lock(gate.mutex);gate.released=true;}gate.cv.notify_all();open(c);
 auto result=wait(batch.get());cd_batch_info_t info{};info.struct_size=sizeof(info);expect(cd_result_get_batch_info(result.get(),&info)==CD_OK,"batch snapshot available");
 expect(snapshot.callbacks==1&&info.was_cancelled&&info.elapsed_ns==snapshot.elapsed,"completed batch result must remain immutable after queued reschedule");
 std::cout<<"PASS cancelled_batch_snapshot\n";
}
static void reverse_cancel(int count){
 auto d=driver();auto c=cli(d,false);auto barrier=cli(d);open(c);
 cd_call_options_t options;cd_call_options_init(&options);options.timeout_ms=60000;
 std::vector<Operation> operations;operations.reserve(count+1);
 for(int i=0;i<=count;++i){cd_operation_t* raw=nullptr;expect(cd_read_bytes_async(c.get(),&options,nullptr,nullptr,&raw)==CD_OK,"queued read");operations.emplace_back(raw,cd_operation_release);}open(barrier);
 auto begin=Clock::now();for(int i=count;i>0;--i)cd_operation_cancel(operations[i].get());open(barrier);
 auto us=std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-begin).count();
 expect(!cd_operation_is_completed(operations[0].get()),"cancelled queue leaves active request intact");
 for(int i=1;i<=count;++i){auto result=wait(operations[i].get());expect(cd_result_get_status(result.get())==CD_CANCELLED,"reverse cancellation status");}close(c);
 std::cout<<"METRIC reverse_cancel count="<<count<<" elapsed_us="<<us<<"\n";
}
static void resource_sample(uint64_t waves,uint64_t operations,Clock::time_point begin){
 std::cout<<"SOAK waves="<<waves<<" operations="<<operations<<" elapsed_ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-begin).count();
#ifdef CD_MALLOC_STATISTICS
 malloc_statistics_t stats{};malloc_zone_statistics(nullptr,&stats);
 std::cout<<" malloc_in_use="<<stats.size_in_use<<" malloc_blocks="<<stats.blocks_in_use;
#endif
#ifdef __GLIBC__
 auto heap=mallinfo2();std::cout<<" malloc_in_use="<<heap.uordblks<<" malloc_mapped="<<heap.hblkhd;
#endif
 std::cout<<std::endl;
}
// Keep one runtime and one uncancelled token alive across waves to expose retained
// registrations. Each accepted operation owns a separate exactly-once counter.
static void cancel_close_soak(int seconds){
 expect(seconds>0,"positive soak duration");auto begin=Clock::now();auto report=begin;
 auto d=driver();std::vector<Connection> connections;
 for(int i=0;i<8;++i){cd_udp_options_t o;cd_udp_options_init(&o);o.remote_host=CD_SV("127.0.0.1");o.remote_port=9;cd_connection_t* c=nullptr;cd_result_t* error=nullptr;auto status=cd_create_udp(d.get(),&o,&c,&error);cd_result_release(error);expect(status==CD_OK,"create soak UDP");connections.emplace_back(c,cd_connection_release);}
 cd_cancel_source_t* raw=nullptr;expect(cd_cancel_source_create(&raw)==CD_OK,"long-lived token");Handle<cd_cancel_source_t,cd_cancel_source_release> retained(raw,cd_cancel_source_release);
 uint64_t waves=0,operations=0;
 do {
  for(auto& c:connections)open(c);
  expect(cd_cancel_source_create(&raw)==CD_OK,"wave token");Handle<cd_cancel_source_t,cd_cancel_source_release> cancel(raw,cd_cancel_source_release);
  struct Slot {std::atomic<int> calls{0};std::atomic<bool> bad{false};};
  constexpr int producers=32,per_producer=128;auto slots=std::make_unique<Slot[]>(producers*per_producer);
  auto callback=[](cd_operation_t*,cd_result_t* r,void* user){auto& s=*static_cast<Slot*>(user);auto status=cd_result_get_status(r);if(status!=CD_OK&&status!=CD_CANCELLED&&status!=CD_CLOSED&&status!=CD_NOT_OPEN&&status!=CD_TIMEOUT)s.bad=true;++s.calls;};
  std::atomic<int> rejected{0};std::barrier start(producers);std::vector<std::thread> threads;
  for(int t=0;t<producers;++t)threads.emplace_back([&,t]{start.arrive_and_wait();for(int i=0;i<per_producer;++i){
   cd_call_options_t o;cd_call_options_init(&o);o.timeout_ms=i%7==0?1:60000;o.cancellation=i%2?cancel.get():retained.get();
   auto* c=connections[t%connections.size()].get();cd_operation_t* op=nullptr;
   auto status=i%17==16?cd_close_async(c,&o,callback,&slots[t*per_producer+i],&op):cd_read_bytes_async(c,&o,callback,&slots[t*per_producer+i],&op);
   if(status!=CD_OK){++rejected;continue;}if(i%3==0)cd_operation_cancel(op);cd_operation_release(op);
   if(i==64)cd_cancel_source_request(cancel.get());
  }});
  for(auto& t:threads)t.join();for(auto& c:connections)close(c);
  expect(rejected==0,"soak accepts every request");
  for(int i=0;i<producers*per_producer;++i)expect(slots[i].calls==1&&!slots[i].bad,"soak exactly-once callback and allowed status");
  ++waves;operations+=producers*per_producer;
  if(Clock::now()>=report){resource_sample(waves,operations,begin);report=Clock::now()+std::chrono::seconds(10);}
 }while(Clock::now()-begin<std::chrono::seconds(seconds));
 cd_cancel_source_request(retained.get());connections.clear();retained.reset();d.reset();resource_sample(waves,operations,begin);
 std::cout<<"PASS cancel_close_soak producers=32 connections=8 operations="<<operations<<std::endl;
}
int main(int argc,char** argv){try{if(argc==3&&std::string(argv[1])=="--soak-seconds")cancel_close_soak(std::stoi(argv[2]));else if(argc==2&&std::string(argv[1])=="--expired")expired_before_admission();else if(argc==2&&std::string(argv[1])=="--precancelled")precancelled_queued();else if(argc==2&&std::string(argv[1])=="--batch-snapshot")cancelled_batch_snapshot();else if(argc==3&&std::string(argv[1])=="--reverse-cancel")reverse_cancel(std::stoi(argv[2]));else if(argc==2&&std::string(argv[1])=="--performance"){for(int count:{2000,4000,8000,16000}){pending_subscriptions(count,false);pending_subscriptions(count,true);registry(count);}}else if(argc==3&&std::string(argv[1])=="--retained")retained_operations(std::stoi(argv[2]));else{expired_before_admission();precancelled_queued();cancelled_batch_snapshot();reverse_cancel(512);pending_subscriptions(512,true,true);registry(512);simultaneous_close();cancellation_storm();duration_limits();retained_operations(1000);lifecycle_churn(argc==2?std::stoi(argv[1]):100);}std::cout<<"PASS native stress\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
