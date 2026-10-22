#pragma once
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>

namespace thorAuto {
// Do not request a missing/broken layer: Android treats a requested layer that
// cannot be discovered as a Vulkan instance creation error, blocking the game.
inline bool providerAvailable(const std::string& directory){
 int fd=open((directory+"/libVkLayer_ThorFG.so").c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
 if(fd<0)return false;
 struct stat info{};unsigned char header[20]{};
 bool ok=fstat(fd,&info)==0&&S_ISREG(info.st_mode)&&(info.st_uid==0||info.st_uid==1000)&&!(info.st_mode&022)&&
   pread(fd,header,sizeof(header),0)==sizeof(header)&&header[0]==0x7f&&header[1]=='E'&&header[2]=='L'&&header[3]=='F'&&
   header[4]==2&&header[5]==1&&header[18]==183&&header[19]==0;
 close(fd);return ok;
}
}
