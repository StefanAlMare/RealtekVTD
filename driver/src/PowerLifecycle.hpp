// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <stdint.h>
namespace realtek_next {
// Called only under the controller lock/gate. Administrative desire survives sleep.
struct PowerLifecycle {
 bool requested=false,suspended=false;uint64_t sleeps=0,wakes=0,failures=0;uint32_t lastStatus=0;
 void request(bool on){requested=on;}
 bool suspend(){if(suspended)return false;suspended=true;++sleeps;return true;}
 bool resume(){if(!suspended)return false;suspended=false;++wakes;return true;}
 bool shouldActivate() const {return requested&&!suspended;}
 void result(uint32_t status){lastStatus=status;if(status)++failures;}
};
}
