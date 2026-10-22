#include "auto_config.hpp"
#include <dlfcn.h>
#include <sys/socket.h>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv){
 if(argc!=4&&argc!=5){std::cerr<<"usage: auto-ipc-test bootstrap.so uid package [--expect-unavailable]\n";return 2;}
 int checks=0;auto check=[&](bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(name);std::cout<<"PASS "<<name<<'\n';};
 void* library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!library)throw std::runtime_error(dlerror());
 auto handler=(void(*)(int))dlsym(library,"zygisk_companion_entry");check(handler,"compiled module exports the companion entry point");
 auto exchange=[&](const thorAuto::Request& request,size_t bytes=sizeof(thorAuto::Request)){
  int sockets[2]{};if(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,sockets))throw std::runtime_error("socketpair");
  timeval timeout{1,0};setsockopt(sockets[0],SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
  std::thread server([&](){handler(sockets[1]);close(sockets[1]);});
  send(sockets[0],&request,bytes,MSG_NOSIGNAL);shutdown(sockets[0],SHUT_WR);
  thorAuto::Response response{};auto count=recv(sockets[0],&response,sizeof(response),MSG_WAITALL);close(sockets[0]);server.join();
  if(count!=sizeof(response))response={};return response;
 };
 thorAuto::Request request{};request.uid=uint32_t(std::stoul(argv[2]));strncpy(request.package,argv[3],sizeof(request.package)-1);
 auto begin=std::chrono::steady_clock::now();auto response=exchange(request);
 if(argc==5){
  check(std::string(argv[4])=="--expect-unavailable"&&!response.selected,"missing runtime is not requested through the real companion");
  std::cout<<"RESULT checks="<<checks<<" failures=0\n";dlclose(library);return 0;
 }
 check(response.selected==1&&thorAuto::providerValid(response.provider),"real companion reads the controller's root-owned configuration");
 auto savedUid=request.uid;request.uid++;check(!exchange(request).selected,"real IPC rejects a different UID");request.uid=savedUid;
 strncat(request.package,".other",sizeof(request.package)-strlen(request.package)-1);check(!exchange(request).selected,"real IPC rejects a package prefix");strncpy(request.package,argv[3],sizeof(request.package)-1);
 request.version=0;check(!exchange(request).selected,"real IPC rejects a wrong protocol version");request.version=thorAuto::magic;
 memset(request.package,'x',sizeof(request.package));check(!exchange(request).selected,"real IPC rejects an unterminated name");
 check(!exchange(request,12).selected,"truncated request terminates without selection");
 double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
 std::cout<<"IPC elapsed_ms="<<elapsed<<" requests=6\nRESULT checks="<<checks<<" failures=0\n";dlclose(library);
}
