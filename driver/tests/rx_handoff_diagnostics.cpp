// SPDX-License-Identifier: GPL-2.0-only
#include "../src/RXHandoffDiagnostics.hpp"
#include <cassert>
#include <cstdio>
using namespace realtek_next;
int main(){
 RXHandoffDiagnostics d;
 assert(!d.inputCalls&&!d.flushReturned);
 for(unsigned i=0;i<64;++i)d.input(1514,0,true,false,true);
 d.flush(64);d.flush(0);
 assert(d.inputCalls==64&&d.inputBytes==64*1514&&d.inputReturned==0);
 assert(d.flushReturned==64&&d.flushCalls==2&&d.emptyFlushes==1&&d.flushMax==64);
 d.input(60,1,false,true,false);d.flush(1);
 assert(d.missingHeader==1&&d.unexpectedNextPacket==1&&d.detachedAtInput==1);
 assert(d.inputReturned==1&&d.flushReturned==65&&d.flushMax==64);
 // No assumption that a method return means BSD has already updated statistics.
 d.inputBytes=UINT32_MAX;d.input(1514,0,true,false,true);
 d.flushReturned=UINT32_MAX;d.flush(512);
 assert(d.inputBytes==uint64_t(UINT32_MAX)+1514);
 assert(d.flushReturned==uint64_t(UINT32_MAX)+512);
 std::puts("PASS RX handoff observations, failures, empty flushes, batching and 64-bit totals");
}
