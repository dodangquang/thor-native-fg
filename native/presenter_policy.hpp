#pragma once
#include <cstring>

// Eden Nightly 6e1b120 with Turnip 26.1.3 repeatedly recreates its swapchain
// with our asynchronous presenter. The fence presenter is verified separately.
// Keep this fallback scoped to the measured package, including its subprocesses.
inline bool defaultFencePresenter(const char* process){
 const char* package="dev.eden.eden_emulator.nightly";
 if(!process)return false;
 auto size=std::strlen(package);
 return !std::strncmp(process,package,size)&&(process[size]==0||process[size]==':');
}
