#include <communicationdriver/communicationdriver.hpp>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>
using namespace std::chrono_literals;
namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
#ifdef _WIN32
static const std::string adb_new_line="\r\n";
#else
static const std::string adb_new_line="\n";
#endif
static std::mutex output_mutex;
static std::atomic<uint64_t> assertions{},commands{},transfers{},transfer_bytes{},stream_bytes{},stream_reads{},recoveries{};
static void event(const std::string& kind,const std::string& detail){std::lock_guard lock(output_mutex);std::cout<<kind<<" "<<detail<<std::endl;}
static void expect(bool value,const std::string& reason){++assertions;if(!value)throw std::runtime_error(reason);}
static std::string quote(const std::string& s){std::string r="'";for(char c:s)r+=c=='\''?"'\\''":std::string(1,c);return r+"'";}
static comm::CallOptions timeout(int ms){comm::CallOptions o;o.timeout=comm::Duration(ms);return o;}
static comm::Connection connect(comm::Driver& d,const std::string& adb,const std::string& serial,bool once=false,std::string session={}){
 comm::AdbOptions o;o.executable_path=adb;o.device_serial=serial;o.execution_mode=once?comm::ExecutionMode::OneShot:comm::ExecutionMode::Persistent;o.session_command=std::move(session);o.max_response_bytes=8*1024*1024;
 o.new_line=adb_new_line;
 auto c=d.create_adb(o);c.open();return c;
}
static auto execute(const comm::Connection& c,const std::string& cmd){auto r=c.execute(cmd,{},timeout(10000));++commands;return r;}
static void exact(const comm::Connection& c,const std::string& cmd,const std::string& want,int code=0){auto r=execute(c,cmd);expect(r.response==want,"unexpected command output expected=["+want+"] actual=["+r.response+"] stderr=["+r.standard_error+"] exit="+(r.exit_code?std::to_string(*r.exit_code):"unknown"));expect(r.exit_code==code,"unexpected exit status");}
static void recover(const comm::Connection& c){c.close();c.open();exact(c,"printf RECOVERED","RECOVERED");++recoveries;}
static std::string read_file(const fs::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static void make_file(const fs::path& p,size_t size){std::ofstream f(p,std::ios::binary);uint32_t x=0x13579bdf;for(size_t n=0;n<size;++n){x^=x<<13;x^=x>>17;x^=x<<5;f.put(static_cast<char>(x));}expect(bool(f),"write local fixture");}
template<class F> static void error(F&& f,cd_status_t code){try{f();throw std::runtime_error("expected operation failure");}catch(const comm::CommunicationError& e){expect(e.info.code==code,"unexpected operation error code "+std::to_string(e.info.code)+" expected "+std::to_string(code));}}
static void functional(comm::Driver& d,const std::string& adb,const std::string& serial,const std::string& label,const fs::path& local,const std::string& remote){
 fs::create_directories(local);auto c=connect(d,adb,serial);execute(c,"mkdir -p "+quote(remote));
 event("STAGE",label+" functional");
 for(bool once:{false,true}){
  auto s=connect(d,adb,serial,once);
  exact(s,"printf READY","READY");exact(s,"printf 'line1\\nline2\\n'","line1"+adb_new_line+"line2"+adb_new_line);exact(s,"true","");exact(s,"false","",1);
  exact(s,"printf '引号 空格'; printf 'ERR' >&2; (exit 7)","引号 空格",7);
  auto err=execute(s,"printf OUT; printf ERR >&2; (exit 9)");expect(err.response=="OUT"&&err.standard_error=="ERR"&&err.exit_code==9,"stdout/stderr/exit isolation");
  auto version=execute(s,"getprop ro.build.version.release; getprop ro.build.version.sdk; getprop ro.build.version.incremental");expect(version.exit_code==0&&!version.response.empty(),"software version query");
  for(auto command:{"id","uname -a","pwd","ls /system/bin/sh","df -k /data/local/tmp","pm path android","dumpsys package android | head -30","getprop ro.build.version.release | tr -d '\\n'"}){auto r=execute(s,command);expect(r.exit_code==0&&!r.response.empty(),"read-only Android command");}
  auto check=s.execute("printf NO",comm::Check::contains("YES"));++commands;expect(check.check&&!check.check->passed&&check.exit_code==0,"normal negative check");
  auto dm=execute(s,"timeout 1 dmesg -w");expect(dm.exit_code.has_value(),"dmesg command completion");event("DMESG",label+" exit="+std::to_string(*dm.exit_code)+" denied="+std::to_string(dm.standard_error.find("permitted")!=std::string::npos));
  auto bytes=s.execute(std::as_bytes(std::span("printf 'A\\000B'",15)));++commands;expect(bytes.response==comm::Bytes{std::byte{'A'},std::byte{0},std::byte{'B'}},"binary output including NUL");
  s.close();
 }
 exact(c,"export CD_HW_STATE=STATE; cd /data/local/tmp; printf \"$CD_HW_STATE\"","STATE");exact(c,"printf \"$CD_HW_STATE\"","STATE");
 auto early=c.execute_until("printf READY; sleep 0.2; printf TAIL",comm::Check::contains("READY"));++commands;expect(early.completion_reason==comm::CompletionReason::Matched&&early.response.find("READY")!=std::string::npos,"early match");exact(c,"printf NEXT","NEXT");
 std::vector<comm::Operation<comm::ExecutionResult<std::string>>> queued;
 for(int n=0;n<32;++n)queued.push_back(c.execute_async("printf Q"+std::to_string(n)));
 for(int n=0;n<32;++n){expect(queued[n].get().response=="Q"+std::to_string(n),"same connection FIFO result ownership");++commands;}
 for(size_t size:{size_t(0),size_t(1),size_t(4095),size_t(65536),size_t(1048576),size_t(16777216)}){
  auto name="source "+std::to_string(size)+" 引号'.bin";auto src=local/fs::path(std::u8string(name.begin(),name.end())),dst=local/("pull "+std::to_string(size)+".bin");make_file(src,size);auto path=remote+"/file "+std::to_string(size)+" 引号'.bin";
  auto source=src.u8string();auto up=c.upload(std::string(source.begin(),source.end()),path),down=c.download(path,dst.string());transfers+=2;transfer_bytes+=size*2;
  expect(up.bytes_transferred==size&&down.bytes_transferred==size,"confirmed transfer length");expect(read_file(src)==read_file(dst),"push/pull byte-for-byte equality");
 }
 {auto once=connect(d,adb,serial,true);once.write_string("printf RAW; sleep 0.1; printf LATE\n");exact(once,"printf OUT","OUT");expect(once.read_exactly_string(7)=="RAWLATE","real ADB OneShot and raw IO isolation");once.close();}
 {auto direct=connect(d,adb,serial,false,"cat");std::string value("DATA\0TAIL",9);auto bytes=std::as_bytes(std::span(value));direct.write_bytes(bytes);expect(direct.read_exactly_bytes(9)==comm::Bytes(bytes.begin(),bytes.end()),"real ADB raw binary session");direct.close();}
 {auto head=c.execute_async("sleep 0.3; printf HEAD");auto queued=c.execute_async("printf NEVER",{},timeout(40));error([&]{queued.get();},CD_TIMEOUT);expect(head.get().response=="HEAD","queued timeout preserves active command");++commands;exact(c,"printf AFTER","AFTER");}
 comm::TransferOptions exclusive;exclusive.overwrite=false;
 auto src=local/"exclusive.bin",dst=local/"exclusive-pull.bin";make_file(src,4096);auto path=remote+"/exclusive.bin";
 c.upload(src.string(),path,exclusive);c.download(path,dst.string(),exclusive);transfers+=2;transfer_bytes+=8192;
 error([&]{c.upload(src.string(),path,exclusive);},CD_PERMISSION_DENIED);error([&]{c.download(path,dst.string(),exclusive);},CD_PERMISSION_DENIED);expect(read_file(src)==read_file(dst),"exclusive transfer preserves content");
 error([&]{c.upload(local.string(),path);},CD_INVALID_ARGUMENT);error([&]{c.download(remote,(local/"directory.bin").string());},CD_INVALID_ARGUMENT);
 try{c.download(remote+"/missing.bin",(local/"missing.bin").string());throw std::runtime_error("missing remote accepted");}catch(const comm::CommunicationError& e){expect(e.info.code!=CD_OK,"missing remote rejected");}recover(c);
 auto interrupted=c.execute_async("printf PART; sleep 30",{},timeout(600));std::this_thread::sleep_for(100ms);interrupted.cancel();error([&]{interrupted.get();},CD_CANCELLED);recover(c);
 try{c.execute("printf PART; sleep 30",{},timeout(150));throw std::runtime_error("timeout missing");}catch(const comm::CommunicationError& e){expect(e.info.code==CD_TIMEOUT&&e.info.partial_text.find("PART")!=std::string::npos,"timeout retains partial output");}recover(c);
 comm::CallOptions limited;limited.max_response_bytes=64;error([&]{c.execute("printf '%01024d' 0",{},limited);},CD_LIMIT_EXCEEDED);recover(c);
 auto pending=c.execute_async("sleep 30",{},timeout(10000));std::this_thread::sleep_for(100ms);std::vector<comm::Operation<void>> closing;for(int n=0;n<16;++n)closing.push_back(c.close_async());for(auto& op:closing)op.get();error([&]{pending.get();},CD_CLOSED);c.open();exact(c,"printf NEXT","NEXT");++recoveries;
 c.close();event("PASS",label+" functional");
}
struct Stats {std::atomic<uint64_t> count{},micros{},max_us{};std::mutex mutex;std::vector<uint64_t> latency;
 void add(Clock::time_point started){auto us=std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-started).count();++count;micros+=us;auto max=max_us.load();while(max<static_cast<uint64_t>(us)&&!max_us.compare_exchange_weak(max,us)){}std::lock_guard lock(mutex);latency.push_back(us);}
 void report(){std::sort(latency.begin(),latency.end());auto at=[&](double p){return latency.empty()?0:latency[std::min(latency.size()-1,static_cast<size_t>(p*latency.size()))];};event("LATENCY","count="+std::to_string(count)+" p50_us="+std::to_string(at(.5))+" p95_us="+std::to_string(at(.95))+" p99_us="+std::to_string(at(.99))+" max_us="+std::to_string(max_us));}
};
struct Load {int command_workers=8,transfer_workers=1,cancel_workers=1,drivers_per_slot=1;};
struct SlotStats {Stats latency;std::atomic<uint64_t> commands{},transfers{},bytes{},reads{},stream_bytes{},recoveries{};};
static void soak(const std::string& adb,const std::vector<std::string>& devices,const fs::path& local,const std::string& remote,int seconds,const Load& load){
 std::atomic<bool> stop=false,go=false;std::atomic<int> ready=0;std::mutex failures_mutex;std::vector<std::string> failures;std::vector<std::future<void>> jobs;
 std::vector<std::shared_ptr<comm::Driver>> drivers;std::vector<std::shared_ptr<SlotStats>> slots;
 auto start_job=[&](std::string label,auto work){jobs.push_back(std::async(std::launch::async,[&,label,work]{try{work();}catch(const std::exception& e){{std::lock_guard lock(failures_mutex);failures.push_back(label+": "+e.what());}stop=true;event("FAIL",label+": "+e.what());}}));};
 Clock::time_point begin,deadline;auto wait_start=[&]{++ready;while(!go&&!stop)std::this_thread::sleep_for(1ms);};
 for(size_t device=0;device<devices.size();++device){
  auto serial=devices[device],label="device-"+std::to_string(device+1);fs::create_directories(local/label);auto path=remote+"/"+label;auto slot=std::make_shared<SlotStats>();slots.push_back(slot);
  std::vector<std::shared_ptr<comm::Driver>> pool;for(int n=0;n<load.drivers_per_slot;++n){auto driver=std::make_shared<comm::Driver>();pool.push_back(driver);drivers.push_back(driver);}
  for(int lane=0;lane<load.command_workers;++lane){auto driver=pool[lane%pool.size()];start_job(label+" command-"+std::to_string(lane),[&,serial,label,lane,slot,driver]{
   auto c=connect(*driver,adb,serial,lane%4==3);uint64_t n=0;wait_start();while(!stop&&Clock::now()<deadline){auto tick=Clock::now();auto marker=label+"-"+std::to_string(lane)+"-"+std::to_string(n++);exact(c,"printf "+quote(marker),marker);auto version=execute(c,"getprop ro.build.version.release; getprop ro.build.version.incremental");expect(version.exit_code==0&&!version.response.empty(),"concurrent version query actual=["+version.response+"] stderr=["+version.standard_error+"] exit="+(version.exit_code?std::to_string(*version.exit_code):"unknown"));slot->commands+=2;slot->latency.add(tick);}c.close();
  });}
  for(int lane=0;lane<load.transfer_workers;++lane){auto driver=pool[lane%pool.size()];start_job(label+" transfer-"+std::to_string(lane),[&,serial,label,path,lane,slot,driver]{
   auto c=connect(*driver,adb,serial);auto name="soak-"+std::to_string(lane)+".bin";auto src=local/label/name,dst=local/label/("pull-"+name);make_file(src,4*1024*1024);auto content=read_file(src);wait_start();
   while(!stop&&Clock::now()<deadline){auto up=c.upload(src.string(),path+"/"+name),down=c.download(path+"/"+name,dst.string());transfers+=2;transfer_bytes+=content.size()*2;expect(up.bytes_transferred==content.size()&&down.bytes_transferred==content.size()&&read_file(dst)==content,"concurrent push/pull content");slot->transfers+=2;slot->bytes+=content.size()*2;}c.close();
  });}
  auto stream_driver=pool.front();
  start_job(label+" logcat",[&,serial,label,slot,stream_driver]{
   auto c=connect(*stream_driver,adb,serial,false,"logcat -b main -v raw -s CDHardware:I");auto producer=connect(*stream_driver,adb,serial);uint64_t n=0;wait_start();
   while(!stop&&Clock::now()<deadline){auto token=label+"-"+remote.substr(remote.rfind('-')+1)+"-log-"+std::to_string(n++);exact(producer,"log -p i -t CDHardware "+quote(token),"");auto r=c.read_until_string(comm::ReceiveUntil::delimiter(token,true),timeout(5000));expect(r.find(token)!=std::string::npos,"real logcat marker");stream_bytes+=r.size();++stream_reads;slot->stream_bytes+=r.size();++slot->reads;std::this_thread::sleep_for(50ms);}c.close();producer.close();
  });
  start_job(label+" continuous-output",[&,serial,label,slot,stream_driver]{
   auto c=connect(*stream_driver,adb,serial,false,"while true; do printf 'CD_STREAM_"+label+"\\n' || exit; sleep 0.02; done");wait_start();while(!stop&&Clock::now()<deadline){auto r=c.read_line(timeout(5000));expect(r=="CD_STREAM_"+label,"continuous stream binding");stream_bytes+=r.size();++stream_reads;slot->stream_bytes+=r.size();++slot->reads;}c.close();
  });
  start_job(label+" dmesg",[&,serial,label,slot,stream_driver]{
   auto c=connect(*stream_driver,adb,serial);auto probe=execute(c,"dmesg >/dev/null");
   if(probe.exit_code==0){c.close();c=connect(*stream_driver,adb,serial,false,"dmesg -w");uint64_t bytes=0,reads=0;wait_start();
    while(!stop&&Clock::now()<deadline){auto pending=c.read_string_async(timeout(std::chrono::duration_cast<std::chrono::milliseconds>(deadline-Clock::now()).count()+5000));while(!pending.wait_for(100ms)&&!stop&&Clock::now()<deadline){}if(pending.is_completed()){auto value=pending.get();expect(!value.empty(),"dmesg continuous stream data");bytes+=value.size();++reads;stream_bytes+=value.size();++stream_reads;slot->stream_bytes+=value.size();++slot->reads;}else{pending.cancel();error([&]{pending.get();},CD_CANCELLED);break;}}
    expect(bytes>0,"real kernel log bytes received");event("DMESG_STREAM",label+" bytes="+std::to_string(bytes)+" reads="+std::to_string(reads));
   }else{wait_start();while(!stop&&Clock::now()<deadline){auto r=execute(c,"dmesg -w");expect(r.exit_code&&*r.exit_code!=0&&!r.standard_error.empty(),"dmesg permission error isolation");std::this_thread::sleep_for(200ms);}}c.close();
  });
  for(int lane=0;lane<load.cancel_workers;++lane){auto driver=pool[lane%pool.size()];start_job(label+" cancel-"+std::to_string(lane),[&,serial,slot,driver]{
   auto c=connect(*driver,adb,serial);wait_start();while(!stop&&Clock::now()<deadline){auto operation=c.execute_async("printf PART; while IFS= read -r __cd_wait; do :; done",{},timeout(5000));std::this_thread::sleep_for(50ms);operation.cancel();error([&]{operation.get();},CD_CANCELLED);recover(c);++slot->recoveries;}c.close();
  });}
 }
 auto startup=Clock::now();while(!stop&&ready<int(jobs.size())){if(Clock::now()-startup>60s){stop=true;event("FAIL","ADB workers did not become ready");break;}std::this_thread::sleep_for(10ms);}bool full_duration=!stop;begin=Clock::now();deadline=begin+std::chrono::seconds(seconds);go=true;event("START","ADB workers="+std::to_string(jobs.size())+" seconds="+std::to_string(seconds));
 while(!stop&&Clock::now()<deadline){std::this_thread::sleep_for(1s);auto elapsed=std::chrono::duration_cast<std::chrono::seconds>(Clock::now()-begin).count();if(elapsed%10==0){event("PROGRESS","seconds="+std::to_string(elapsed)+" commands="+std::to_string(commands)+" transfers="+std::to_string(transfers)+" stream_reads="+std::to_string(stream_reads)+" recoveries="+std::to_string(recoveries));for(size_t n=0;n<slots.size();++n){auto& s=*slots[n];event("SLOT_PROGRESS","device-"+std::to_string(n+1)+" commands="+std::to_string(s.commands)+" transfers="+std::to_string(s.transfers)+" reads="+std::to_string(s.reads)+" recoveries="+std::to_string(s.recoveries));}}}
 full_duration=full_duration&&!stop;stop=true;for(auto& job:jobs)job.get();for(auto& driver:drivers)driver->close();
 for(size_t n=0;n<slots.size();++n){auto& s=*slots[n];event("SLOT","device-"+std::to_string(n+1));s.latency.report();event("SLOT_SUMMARY","device-"+std::to_string(n+1)+" commands="+std::to_string(s.commands)+" transfers="+std::to_string(s.transfers)+" transfer_bytes="+std::to_string(s.bytes)+" stream_reads="+std::to_string(s.reads)+" stream_bytes="+std::to_string(s.stream_bytes)+" recoveries="+std::to_string(s.recoveries));}
 expect(full_duration&&failures.empty(),failures.empty()?"full duration with all ADB workers":"soak worker failed: "+failures.front());event("PASS","mixed-soak seconds="+std::to_string(seconds));
}
int main(int argc,char** argv){
 if(argc<6){std::cerr<<"Use: cd_adb_hardware_tests <adb> <local-root> <remote-root> <seconds> <serial> <serial>...\n";return 2;}
 std::string checked_root=argv[3];std::string prefix="/data/local/tmp/cd-hardware-";if(!checked_root.starts_with(prefix)||checked_root.size()==prefix.size()||!std::all_of(checked_root.begin()+prefix.size(),checked_root.end(),[](char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-';})){std::cerr<<"Invalid remote test root\n";return 2;}
 comm::Driver driver;bool soak_only=false,allow_single=false;std::string offline_device;Load load;std::vector<std::string> devices;for(int n=5;n<argc;++n){auto arg=std::string(argv[n]);if(arg=="--soak-only")soak_only=true;else if(arg=="--allow-single-device")allow_single=true;else if(arg=="--offline-device"&&n+1<argc)offline_device=argv[++n];else if(arg.starts_with("--")&&n+1<argc){int value=std::stoi(argv[++n]);if(value<1)throw std::runtime_error("worker count must be positive");if(arg=="--command-workers")load.command_workers=value;else if(arg=="--transfer-workers")load.transfer_workers=value;else if(arg=="--cancel-workers")load.cancel_workers=value;else if(arg=="--drivers-per-slot")load.drivers_per_slot=value;else throw std::runtime_error("unknown load option");}else devices.push_back(argv[n]);}auto local=fs::absolute(argv[2]);std::string remote=argv[3];int rc=0;
 try{expect(devices.size()>=(allow_single?1:2),"required physical devices online");if(!offline_device.empty()){error([&]{auto c=connect(driver,argv[1],offline_device);c.close();},CD_DEVICE_UNAVAILABLE);event("PASS","disconnected device returns DeviceUnavailable");}for(size_t n=0;n<devices.size();++n){auto c=connect(driver,argv[1],devices[n]);execute(c,"mkdir -p "+quote(remote+"/device-"+std::to_string(n+1))+"; printf device-"+std::to_string(n+1)+" > "+quote(remote+"/binding"));c.close();}if(!soak_only)for(size_t n=0;n<devices.size();++n)functional(driver,argv[1],devices[n],"device-"+std::to_string(n+1),local/("device-"+std::to_string(n+1)),remote+"/device-"+std::to_string(n+1));std::vector<comm::Connection> batch_connections;std::vector<comm::BatchItem> items;for(size_t n=0;n<devices.size();++n){batch_connections.push_back(connect(driver,argv[1],devices[n]));auto label="device-"+std::to_string(n+1);exact(batch_connections.back(),"cat "+quote(remote+"/binding"),label);items.push_back(comm::BatchItem::execute(label,batch_connections.back(),"cat "+quote(remote+"/binding"),comm::Check::equals(label)));}auto batch=comm::Batch::run(items);expect(batch.all_succeeded&&batch.items.size()==devices.size(),"bound-device batch binding");commands+=items.size();for(auto& c:batch_connections)c.close();event("PASS",devices.size()>1?"cross-device binding and Batch":"single-device binding and Batch");int seconds=std::stoi(argv[4]);if(seconds>0)soak(argv[1],devices,local,remote,seconds,load);}
 catch(const comm::CommunicationError& e){event("FAIL","native-status="+std::to_string(e.info.code)+" "+e.what());rc=1;}catch(const std::exception& e){event("FAIL",e.what());rc=1;}
 for(auto& serial:devices){try{auto c=connect(driver,argv[1],serial);execute(c,"rm -rf "+quote(remote));c.close();}catch(const std::exception& e){event("CLEANUP_FAIL",e.what());rc=1;}}
 driver.close();event("SUMMARY","assertions="+std::to_string(assertions)+" commands="+std::to_string(commands)+" transfers="+std::to_string(transfers)+" transfer_bytes="+std::to_string(transfer_bytes)+" stream_reads="+std::to_string(stream_reads)+" stream_bytes="+std::to_string(stream_bytes)+" recoveries="+std::to_string(recoveries)+" exit="+std::to_string(rc));return rc;
}
