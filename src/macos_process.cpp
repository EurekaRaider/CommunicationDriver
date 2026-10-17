#include "core.hpp"
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

namespace cd {
namespace {
void check_spawn(int error) {
 if(error)throw Fault(CD_CONNECTION_FAILED,std::strerror(error),error);
}
struct SpawnSetup {
 posix_spawn_file_actions_t actions;
 posix_spawnattr_t attributes;
 SpawnSetup() {
  check_spawn(posix_spawn_file_actions_init(&actions));
  int error=posix_spawnattr_init(&attributes);
  if(error){posix_spawn_file_actions_destroy(&actions);check_spawn(error);}
 }
 ~SpawnSetup(){posix_spawnattr_destroy(&attributes);posix_spawn_file_actions_destroy(&actions);}
};
struct ChildDescriptors {
 std::array<int,3> values{-1,-1,-1};
 ~ChildDescriptors(){for(int fd:values)if(fd>=0)::close(fd);}
};
}

std::shared_ptr<process::process> spawn_process_macos(asio::io_context& io,const process::filesystem::path& executable,std::vector<std::string>& args,std::vector<std::string>& environment,const std::filesystem::path& directory,asio::writable_pipe& input,asio::readable_pipe& out,asio::readable_pipe& err) {
 // No user-space fork child: inherited ObjC/allocator hooks are unsafe while
 // independent runtimes launch processes concurrently (including under ASan).
 asio::readable_pipe child_input(io);
 asio::writable_pipe child_output(io),child_error(io);
 asio::connect_pipe(child_input,input);
 asio::connect_pipe(out,child_output);
 asio::connect_pipe(err,child_error);
 SpawnSetup setup;ChildDescriptors descriptors;
 std::array<int,3> sources{child_input.native_handle(),child_output.native_handle(),child_error.native_handle()};
 for(size_t i=0;i<sources.size();++i) {
  // Keep redirect sources above stdio, even if the caller closed fd 0/1/2.
  descriptors.values[i]=fcntl(sources[i],F_DUPFD_CLOEXEC,3);
  if(descriptors.values[i]<0)check_spawn(errno);
 }
 for(size_t i=0;i<descriptors.values.size();++i) {
  check_spawn(posix_spawn_file_actions_adddup2(&setup.actions,descriptors.values[i],static_cast<int>(i)));
  check_spawn(posix_spawn_file_actions_addclose(&setup.actions,descriptors.values[i]));
 }
 // Only the explicitly redirected descriptors may survive exec.
 check_spawn(posix_spawnattr_setflags(&setup.attributes,POSIX_SPAWN_CLOEXEC_DEFAULT));
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
 check_spawn(posix_spawn_file_actions_addchdir_np(&setup.actions,directory.c_str()));
#pragma clang diagnostic pop
 auto path=executable.string();
 std::vector<char*> argv{path.data()},envp;
 for(auto& arg:args)argv.push_back(arg.data());argv.push_back(nullptr);
 for(auto& entry:environment)envp.push_back(entry.data());envp.push_back(nullptr);
 pid_t pid;
 check_spawn(posix_spawn(&pid,path.c_str(),&setup.actions,&setup.attributes,argv.data(),envp.data()));
 try {return std::make_shared<process::process>(io.get_executor(),pid);}
 catch(...) {
  kill(pid,SIGKILL);
  while(waitpid(pid,nullptr,0)<0&&errno==EINTR){}
  throw;
 }
}
}
