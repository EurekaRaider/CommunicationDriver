#include <communicationdriver/communicationdriver.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>
static std::string env(const char* name){auto v=std::getenv(name);if(!v)throw std::runtime_error(std::string("Required: ")+name);return v;}
static void expect(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
static std::string read(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char** argv){try {
 comm::Driver driver;auto root=std::filesystem::absolute("artifacts/integration");std::filesystem::create_directories(root);
 auto source=root/"source.bin",remote=root/"remote.bin",destination=root/"download.bin";
 std::string content(1024*1024,'\0');for(size_t i=0;i<content.size();++i)content[i]=static_cast<char>((i*17)%256);{std::ofstream f(source,std::ios::binary);f.write(content.data(),content.size());}
 if(argc>1&&std::string(argv[1])=="--ssh"){
  comm::SshOptions options;options.host="127.0.0.1";options.port=22482;options.user_name=env("CD_SSH_USER");options.auth=comm::SshAuth::private_key(env("CD_SSH_PRIVATE_KEY"));options.host_key=comm::HostKey::sha256(env("CD_SSH_FINGERPRINT"));
  auto shell=driver.create_ssh(options);shell.open();auto r=shell.execute("printf READY",comm::Check::contains("READY"));expect(r.response=="READY"&&r.check->passed,"SSH persistent no newline");
  shell.execute("cd /tmp; export CD_SSH=STATE");expect(shell.execute("printf \"$CD_SSH\"").response=="STATE","SSH session state");
  auto early=shell.execute_until("printf READY; sleep 0.1; printf TAIL",comm::Check::contains("READY"));expect(early.completion_reason==comm::CompletionReason::Matched,"SSH early match");expect(shell.execute("printf NEXT").response=="NEXT","SSH cleanup ownership");
  auto uploaded=shell.upload(source.string(),remote.string());expect(uploaded.bytes_transferred==content.size(),"SFTP confirmed progress");auto downloaded=shell.download(remote.string(),destination.string());expect(downloaded.bytes_transferred==content.size()&&read(destination)==content,"SFTP file equality");
  comm::TransferOptions no_replace;no_replace.overwrite=false;try{shell.upload(source.string(),remote.string(),no_replace);throw std::runtime_error("overwrite incorrectly succeeded");}catch(const comm::CommunicationError& e){expect(e.info.code!=CD_OK,"SFTP overwrite refusal");}
  shell.close();shell.open();auto large=root/"large.bin",partial=root/"partial.bin";{std::ofstream file(large,std::ios::binary);file.seekp(128*1024*1024-1);file.put('\0');}
  comm::TransferOptions interrupted;interrupted.timeout=comm::Duration(25);try{shell.download(large.string(),partial.string(),interrupted);throw std::runtime_error("large transfer unexpectedly completed before interruption");}catch(const comm::CommunicationError& e){expect(e.info.code==CD_TIMEOUT&&!e.info.partial_file.empty(),"SFTP interruption reports partial file");}shell.close();auto partial_size=std::filesystem::file_size(partial);expect(partial_size<128*1024*1024,"SFTP interrupted download is partial");shell.open();expect(shell.execute("printf NEXT").response=="NEXT"&&std::filesystem::file_size(partial)==partial_size,"close waits for local file worker and stops interrupted transfer");shell.close();
  options.execution_mode=comm::ExecutionMode::OneShot;auto once=driver.create_ssh(options);once.open();once.write_string("printf RAW; sleep 0.1; printf LATE\n");auto one=once.execute("printf OUT; printf ERR >&2; exit 7");expect(one.response=="OUT"&&one.standard_error=="ERR"&&one.exit_code==7,"SSH command channel stderr and real exit status");expect(once.read_exactly_string(7)=="RAWLATE","SSH oneshot output is isolated from persistent stdio");expect(once.execute("printf SECOND").response=="SECOND","SSH authentication reused after raw IO");once.close();
  options.host_key=comm::HostKey::sha256("SHA256:INVALID");auto denied=driver.create_ssh(options);try{denied.open();throw std::runtime_error("wrong host key accepted");}catch(const comm::CommunicationError& e){expect(e.info.code==CD_HOST_KEY_REJECTED,"strict host key refusal");}denied.close();
  std::cout<<"PASS OpenSSH native persistent/oneshot, strict host key, state, cleanup, SFTP and file equality\n";
 }else if(argc>2&&std::string(argv[1])=="--adb"){
  comm::AdbOptions options;options.device_serial="DEVICE_A";options.executable_path=std::filesystem::absolute(argv[2]).string();auto a=driver.create_adb(options);options.device_serial="DEVICE_B";auto b=driver.create_adb(options);a.open();b.open();expect(a.execute("printf READY").response=="READY","ADB persistent no newline");expect(b.execute("printf SECOND").response=="SECOND","ADB second bound device");
  expect(a.execute("export CD_ADB=STATE").exit_code==0,"ADB managed exit status");expect(a.execute("printf \"$CD_ADB\"").response=="STATE","ADB session state");auto u=a.upload(source.string(),remote.string());auto down=b.download(remote.string(),destination.string());expect(read(destination)==content&&u.bytes_transferred==content.size()&&down.bytes_transferred==content.size(),"ADB single file transfer");a.close();b.close();
  options.execution_mode=comm::ExecutionMode::OneShot;auto once=driver.create_adb(options);once.open();auto r=once.execute("printf READY; exit 7");expect(r.response=="READY"&&r.exit_code==7,"ADB oneshot real process exit");once.close();options.device_serial="OFFLINE";auto offline=driver.create_adb(options);try{offline.open();throw std::runtime_error("offline device accepted");}catch(const comm::CommunicationError& e){expect(e.info.code==CD_DEVICE_UNAVAILABLE,"ADB unavailable device");}offline.close();std::cout<<"PASS simulated adb executable binding, persistent/oneshot, server arguments, file equality (not real ADB hardware)\n";
 }else throw std::runtime_error("Use --ssh or --adb <fake_adb executable>");
 return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
