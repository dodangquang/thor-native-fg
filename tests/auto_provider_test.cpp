#include "auto_provider.hpp"
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>

int main(int argc,char** argv){
 if(argc!=2)return 2;
 int checks=0;auto check=[&](bool value,const char* name){++checks;if(!value)throw std::runtime_error(name);std::cout<<"PASS "<<name<<'\n';};
 check(thorAuto::providerAvailable(argv[1]),"installed signed ARM64 runtime is available");
 std::string dir="/data/local/tmp/thor-provider-test-"+std::to_string(getpid());
 if(mkdir(dir.c_str(),0700))return 2;
 std::string path=dir+"/libVkLayer_ThorFG.so";
 check(!thorAuto::providerAvailable(dir),"missing library cannot be requested");
 int fd=open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL,0644);if(fd<0)return 2;
 check(!thorAuto::providerAvailable(dir),"empty library cannot be requested");
 unsigned char header[20]{0x7f,'E','L','F',2,1};header[18]=183;
 if(write(fd,header,sizeof(header))!=sizeof(header))return 2;
 check(thorAuto::providerAvailable(dir),"regular root-owned ARM64 ELF passes availability check");
 fchmod(fd,0666);check(!thorAuto::providerAvailable(dir),"group or world writable library is rejected");fchmod(fd,0644);
 header[18]=62;pwrite(fd,header,sizeof(header),0);check(!thorAuto::providerAvailable(dir),"wrong CPU architecture is rejected");
 close(fd);unlink(path.c_str());symlink((std::string(argv[1])+"/libVkLayer_ThorFG.so").c_str(),path.c_str());
 check(!thorAuto::providerAvailable(dir),"library symlink cannot bypass the regular-file check");unlink(path.c_str());rmdir(dir.c_str());
 std::cout<<"RESULT checks="<<checks<<" failures=0\n";
}
