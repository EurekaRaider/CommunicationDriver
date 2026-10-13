// Test double for command-line binding and file behavior. It is not an ADB implementation.
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#ifndef _WIN32
#include <sys/wait.h>
#endif
int main(int argc,char** argv){std::vector<std::string> a(argv+1,argv+argc);if(a.size()<7||a[0]!="-H"||a[2]!="-P"||a[4]!="-s"){std::cerr<<"Explicit server and device binding required\n";return 10;}
 if(auto log=std::getenv("CD_FAKE_ADB_LOG")){std::ofstream f(log,std::ios::app);for(auto& arg:a)f<<arg<<"|";f<<"\n";}
 if(a[5]!="DEVICE_A"&&a[5]!="DEVICE_B"){std::cerr<<"device offline\n";return 1;}
 if(a[6]=="get-state"){std::cout<<"device\n";return 0;}
 if(a[6]=="shell"&&a.size()==9&&a[7]=="-T"){int rc=std::system(a[8].c_str());
#ifndef _WIN32
 return WIFEXITED(rc)?WEXITSTATUS(rc):128;
#else
 return rc;
#endif
 }
 if((a[6]=="push"||a[6]=="pull")&&a.size()==9){try{std::filesystem::copy_file(a[7],a[8],std::filesystem::copy_options::overwrite_existing);std::cout<<"1 file transferred\n";return 0;}catch(const std::exception& e){std::cerr<<e.what();return 1;}}
 std::cerr<<"Unsupported simulated command\n";return 2;
}
