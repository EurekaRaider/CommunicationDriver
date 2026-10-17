#include <communicationdriver/communicationdriver.hpp>
#include <atomic>
#include <barrier>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>
int main(int argc,char** argv){try{
 int workers=argc>1?std::stoi(argv[1]):8,rounds=argc>2?std::stoi(argv[2]):100;
 std::barrier start(workers);std::atomic<int> completed{},finished{};std::vector<std::future<void>> jobs;
 for(int n=0;n<workers;++n)jobs.push_back(std::async(std::launch::async,[&]{
  comm::Driver driver;comm::CliOptions options;options.execution_mode=comm::ExecutionMode::OneShot;
#ifdef _WIN32
  const std::string command="echo|set /p=READY";
#else
  const std::string command="printf READY";
#endif
  auto c=driver.create_cli(options);c.open();start.arrive_and_wait();
  for(int i=0;i<rounds;++i){auto result=c.execute(command);if(result.response!="READY"||result.exit_code!=0)throw std::runtime_error("concurrent process output/exit mismatch");++completed;}
  c.close();driver.close();++finished;
 }));
 while(finished<workers){std::this_thread::sleep_for(std::chrono::seconds(1));std::cout<<"PROGRESS processes="<<completed<<std::endl;for(auto& job:jobs)if(job.valid()&&job.wait_for(std::chrono::seconds(0))==std::future_status::ready)job.get();}
 for(auto& job:jobs)if(job.valid())job.get();std::cout<<"PASS concurrent drivers="<<workers<<" processes="<<completed<<std::endl;return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}}
