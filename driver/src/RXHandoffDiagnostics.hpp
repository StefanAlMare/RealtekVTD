// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <stdint.h>
namespace realtek_next {
// Observations only. Calls are not acknowledgements from the BSD input thread.
struct RXHandoffDiagnostics {
 uint64_t calls=0, allocationsFailed=0, copiesFailed=0, interfaceAbsent=0;
 uint64_t inputCalls=0, inputBytes=0, inputReturned=0;
 uint64_t missingHeader=0, unexpectedNextPacket=0, detachedAtInput=0;
 uint64_t flushCalls=0, flushReturned=0, emptyFlushes=0, flushMax=0;
 void input(unsigned bytes,unsigned returned,bool header,bool next,bool attached){
  ++inputCalls;inputBytes+=bytes;inputReturned+=returned;
  missingHeader+=!header;unexpectedNextPacket+=next;detachedAtInput+=!attached;
 }
 void flush(unsigned returned){
  ++flushCalls;flushReturned+=returned;emptyFlushes+=returned==0;
  if(returned>flushMax)flushMax=returned;
 }
};
}
