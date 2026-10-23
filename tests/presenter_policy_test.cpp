#include "../native/presenter_policy.hpp"
#include <cassert>
#include <cstdio>
int main(){
 assert(defaultFencePresenter("dev.eden.eden_emulator.nightly"));
 assert(defaultFencePresenter("dev.eden.eden_emulator.nightly:renderer"));
 assert(!defaultFencePresenter("dev.eden.eden_emulator.nightly.other"));
 assert(!defaultFencePresenter("dev.eden.eden_emulator"));
 assert(!defaultFencePresenter("dev.igawa6.edenduo"));
 assert(!defaultFencePresenter("com.droiddeck.launcher"));
 assert(!defaultFencePresenter(""));assert(!defaultFencePresenter(nullptr));
 std::puts("8 presenter package policy checks PASS");
}
