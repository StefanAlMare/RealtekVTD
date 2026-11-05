// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "DMAProgram.hpp"
#include "StopSafety.hpp"
#include "JTXTrace.hpp"
#include "TXChecksum.hpp"
#include "TSO4.hpp"
#include "TSO6.hpp"
#include "RXChecksum.hpp"
#include "MAC44RXChecksum.hpp"
namespace realtek_next {
constexpr unsigned kNetworkTXCount=64, kNetworkRXCount=64, kNetworkBufferSize=2048;
constexpr uint16_t kNetworkInterruptMask=0x000f;
enum class NetworkStatus:uint32_t { Ok, NotReady, Allocation, Mapping, IO, Sync, InvalidPacket, Full, UnsafeStop, ChecksumRequest };
class PacketSink {
public:
 virtual ~PacketSink()=default;
 // Return false when the network stack cannot accept a copied frame.
 virtual bool receive(const uint8_t *,unsigned)=0;
 virtual bool receive(const uint8_t *b,unsigned n,const RXChecksumMetadata &){return receive(b,n);}
 virtual void releasePacket(void *)=0;
};
struct NetworkResult {
 StopSafety stopSafety;
 NetworkStatus status=NetworkStatus::NotReady;
 bool prepared=false,running=false,unsafeRetained=false;
 volatile uint32_t *tx=nullptr,*rx=nullptr;
 DMABuffer rxBuffers[kNetworkRXCount],txPackets[kTXDescriptorMaxCount];
 void *cookies[kTXDescriptorMaxCount]{};
 JTXTrace *trace=nullptr;uint64_t traceIDs[kTXDescriptorMaxCount]{};
 unsigned txCount=kNetworkTXCount;
 unsigned producer=0,consumer=0,used=0,rxConsumer=0;
 // P12 evidence written only when submission returns Full; no additional descriptor reads.
 unsigned txFullReason=0,txFullIndex=0,txFullRequested=0;uint32_t txFullWord=0;
 unsigned mtu=1500,bufferSize=kNetworkBufferSize;
 unsigned macVersion=44;
 RXChecksumProfile rxChecksumProfile=RXChecksumProfile::GU25RevisionC;
 uint64_t mac44RXObserved4=0,mac44RXObserved6=0,mac44RXObservedOther=0;
 MAC44RXHardwareReceipt mac44RXHardware;uint64_t mac44RXHardwareAttempts=0,mac44RXHardwareFailures=0;
 bool mac34Native64Verified=false,mac34TXAuthorized=false;uint64_t mac34TXCancelled=0,mac34RXHardwareAttempts=0,mac34RXHardwareFailures=0;
 // Exact-MAC46 authorization receipts, selected only after completed backend mode.
 // Native64 is distinct from MapperReady; absent mapper alone is never proof.
 bool mac46MapperVerified=false,mac46Native64Verified=false,mac46TXAuthorized=false;
 bool mac46TSO4Authorized=false;uint32_t mac46TSO4Limit=0;
 bool mac46TSO6Authorized=false;uint32_t mac46TSO6Limit=0;
 MAC46RXHardwareReceipt mac46RXHardware;uint64_t mac46RXHardwareAttempts=0,mac46RXHardwareFailures=0;
 bool mac44Native64ModeVerified=false; // scoped exact-MAC44 backend selection receipt
 bool mac44TSO4Authorized=false;uint32_t mac44TSO4Limit=0;
 bool mac44TSO6Authorized=false;uint32_t mac44TSO6Limit=0;
 bool rxChecksumRequested=false,rxChecksumActive=false,rxChecksum6Requested=false;RXChecksumHardwareReceipt rxChecksumHardware;RXChecksumStats rxChecksumStats;
 uint64_t txSubmitted=0,txCompleted=0,rxDelivered=0,rxDropped=0,irqActions=0,txErrors=0;
};
bool prepareNetwork(DMAProvider &,Stage2AResult &,NetworkResult &,unsigned bufferSize=kNetworkBufferSize);
bool discardPreparedNetwork(DMAProvider &,Stage2AResult &,NetworkResult &);
bool startNetwork(RegisterIO &,DMAProvider &,Stage2AResult &,NetworkResult &,bool promiscuous=false);
NetworkStatus submitNetwork(RegisterIO &,DMAProvider &,Stage2AResult &,NetworkResult &,DMABuffer &,void *,uint64_t traceID=0,const TXChecksumPlan &checksum=TXChecksumPlan{},const TSO4HardwarePlan &tso=TSO4HardwarePlan{},const TSO6HardwarePlan &tso6=TSO6HardwarePlan{});
bool serviceNetwork(RegisterIO &,DMAProvider &,Stage2AResult &,NetworkResult &,PacketSink &,uint16_t);
bool stopNetwork(RegisterIO &,DMAProvider &,Stage2AResult &,NetworkResult &,PacketSink &,PCIConfig *stopPCI=nullptr);
bool releaseNetwork(RegisterIO &,DMAProvider &,Stage2AResult &,NetworkResult &,PacketSink &,PCIConfig *stopPCI=nullptr);
}
