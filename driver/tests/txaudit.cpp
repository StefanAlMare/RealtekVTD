#include "../src/TXAudit.hpp"
#include <cassert>
#include <cstdio>
using namespace realtek_next;
int main(){TXAudit a;
 a.mapped(DMAStatus::Ok,500,2,2,1514,0);a.mapped(DMAStatus::MapFailed,2000000,0,5,1514,3);
 a.drop(TXDrop::Mapping);a.drop(TXDrop::Size);
 assert(a.mapCalls==2&&a.mapStatus[0]==1&&a.mapStatus[unsigned(DMAStatus::MapFailed)]==1);
 assert(a.mapTotalNS==2000500&&a.mapMaxNS==2000000&&a.mapOver1MS==1);
 assert(a.chainMax==5&&a.segments[2]==1&&a.segments[0]==1&&a.pendingChecksum==1&&a.checksumFlags==3);
 assert(a.totalDrops()==2&&a.drops[3]==1&&a.drops[1]==1);
 puts("PASS: TX audit time, failure attribution, checksum requests and segment accounting");}
