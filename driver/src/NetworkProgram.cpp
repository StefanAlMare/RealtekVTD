// SPDX-License-Identifier: GPL-2.0-only
#include "NetworkProgram.hpp"
#include "FramePolicy.hpp"
#include "JumboProfile.hpp"
namespace realtek_next {
namespace {
constexpr uint32_t Own=1U<<31,End=1U<<30,First=1U<<29,Last=1U<<28;
constexpr uint32_t RxErrors=(1U<<22)|(1U<<21)|(1U<<20)|(1U<<19);
constexpr uint32_t RxConfig=(1U<<15)|(1U<<14)|(7U<<8)|(1U<<11)|14;
void cleanup(DMAProvider &d,DMABuffer &b){
 if(b.mapped)d.unmap(b);if(b.prepared)d.complete(b);if(b.opaque||b.allocated)d.release(b);
}
uint32_t end(unsigned i,unsigned count){return i+1==count?End:0;}
// No new provider virtual or inferred native readiness: the controller supplies
// the completed native64 receipt; the core independently rechecks actual mode.
bool mac46DMAAuthorized(const DMAProvider &d,const NetworkResult &n){
 return d.dmaReady()&&(d.deviceMapperPresent()?n.mac46MapperVerified:
  (!n.mac46MapperVerified&&n.mac46Native64Verified));
}
bool writeVerify(RegisterIO &io,uint16_t o,uint32_t v){uint32_t got=0;return io.write32(o,v)&&io.read32(o,got)&&got==v;}
bool jtxDoorbell(RegisterIO &io,NetworkResult &n,uint64_t id){
 const bool ok=io.write8(0x38,0x40);if(n.trace)n.trace->doorbell(id,ok);return ok;
}
DMAStatus jtxSync(DMAProvider &d,DMABuffer &b,NetworkResult &n,uint64_t id,bool ring){
 const auto status=d.syncForDevice(b);if(n.trace)n.trace->synchronized(id,ring,status==DMAStatus::Ok);return status;
}
}
bool prepareNetwork(DMAProvider &d,Stage2AResult &a,NetworkResult &n,unsigned bufferSize){
 if(n.prepared||!a.complete||!d.dmaReady()||!txDescriptorCountValid(a.txDescriptorCount)||
    a.tx.mappedLength!=uint64_t(a.txDescriptorCount)*kDescriptorSize||(bufferSize!=kNetworkBufferSize&&bufferSize!=kJumboRXBytes))return false;
 n.txCount=a.txDescriptorCount;n.bufferSize=bufferSize;
 n.tx=static_cast<volatile uint32_t*>(d.bytes(a.tx));n.rx=static_cast<volatile uint32_t*>(d.bytes(a.rx));
 if(!n.tx||!n.rx)return false;
 for(unsigned i=0;i<kNetworkRXCount;++i){auto &b=n.rxBuffers[i];
  if(d.allocate(b,n.bufferSize,16,DMADirection::FromDevice)!=DMAStatus::Ok||
     d.prepare(b)!=DMAStatus::Ok||d.map(b)!=DMAStatus::Ok||
     b.segmentCount!=1||b.mappedLength!=n.bufferSize||b.segments[0].length!=n.bufferSize||
     !b.segments[0].address||b.segments[0].address>UINT64_MAX-(n.bufferSize-1)||!d.bytes(b)){
   n.status=NetworkStatus::Mapping;
   for(auto &allocated:n.rxBuffers)cleanup(d,allocated);
   return false;
  }
 }
 n.prepared=true;n.status=NetworkStatus::Ok;return true;
}
bool discardPreparedNetwork(DMAProvider &d,Stage2AResult &a,NetworkResult &n){
 if(a.busMasterEnabled||n.running||n.used||n.unsafeRetained)return false;
 for(auto &b:n.rxBuffers)cleanup(d,b);n.prepared=false;n.tx=n.rx=nullptr;return true;
}
bool startNetwork(RegisterIO &io,DMAProvider &d,Stage2AResult &a,NetworkResult &n,bool promisc){
 if(!n.prepared||n.running||n.unsafeRetained||n.used||!txDescriptorCountValid(n.txCount)||
    n.txCount!=a.txDescriptorCount||a.tx.mappedLength!=uint64_t(n.txCount)*kDescriptorSize||
    (n.bufferSize!=kNetworkBufferSize&&n.bufferSize!=kJumboRXBytes)||n.mtu<1500||n.mtu>kJumboMTU||
    maxFrameBytes(n.mtu)+4>n.bufferSize)return false;
 n.rxChecksumActive=false;n.rxChecksumHardware={};n.mac44RXHardware={};
 uint8_t command=255;
 if(!io.write16(0x3c,0)||!io.write8(0x37,0)||!io.read8(0x37,command)||command&12){n.status=NetworkStatus::IO;return false;}
 n.producer=n.consumer=n.used=n.rxConsumer=0;
 for(unsigned i=0;i<n.txCount*4;++i)n.tx[i]=0;
 for(unsigned i=0;i<n.txCount;++i)n.traceIDs[i]=0;
 for(unsigned i=0;i<kNetworkRXCount;++i){auto &b=n.rxBuffers[i];
  if(d.syncForDevice(b)!=DMAStatus::Ok){n.status=NetworkStatus::Sync;return false;}
  n.rx[4*i+2]=uint32_t(b.segments[0].address);n.rx[4*i+3]=uint32_t(b.segments[0].address>>32);
  n.rx[4*i+1]=0;n.rx[4*i]=n.bufferSize|end(i,kNetworkRXCount)|Own;
 }
 d.memoryBarrier();
 if(d.syncForDevice(a.tx)!=DMAStatus::Ok||d.syncForDevice(a.rx)!=DMAStatus::Ok){n.status=NetworkStatus::Sync;return false;}
 const uint64_t tx=a.tx.segments[0].address,rx=a.rx.segments[0].address;
 uint32_t config=0;uint16_t size=0,cplus=0;uint8_t maxPacket=0;
 // Policy is set after checked hardware init; MAC44 requires its explicit ready-DeviceMapper profile.
 // Jumbo retains its existing software checksum path; VLAN stripping stays OFF.
 const bool mac44RX=n.rxChecksumProfile==RXChecksumProfile::MAC44RevisionCDeviceMapper&&n.macVersion==44;
 const bool mac34RX=n.rxChecksumProfile==RXChecksumProfile::MAC34RevisionC&&n.macVersion==34&&n.mac34Native64Verified&&d.dmaReady()&&!d.deviceMapperPresent();
 const bool mac46RX=n.rxChecksumProfile==RXChecksumProfile::MAC46RevisionC&&n.macVersion==46&&mac46DMAAuthorized(d,n);
 const bool checksum=(n.rxChecksumRequested||n.rxChecksum6Requested)&&
  ((n.rxChecksumProfile==RXChecksumProfile::GU25RevisionC&&n.macVersion==42)||mac44RX||mac34RX||mac46RX)&&n.mtu==1500;
 auto configureRX=[&](){
  if(mac46RX){
   ++n.mac46RXHardwareAttempts;const bool ok=configureMAC46RXHardware(io,checksum,n.rxChecksumHardware,n.mac46RXHardware);
   if(n.mac46RXHardware.status!=MAC44RXHardwareStatus::Ok)++n.mac46RXHardwareFailures;
   n.rxChecksumActive=checksum&&n.rxChecksumHardware.verified;return ok;
  }
  if(mac34RX){
   ++n.mac34RXHardwareAttempts;const bool ok=configureMAC44RXHardware(io,checksum,n.rxChecksumHardware,n.mac44RXHardware);
   if(n.mac44RXHardware.status!=MAC44RXHardwareStatus::Ok)++n.mac34RXHardwareFailures;
   n.rxChecksumActive=checksum&&n.rxChecksumHardware.verified;return ok;
  }
  if(mac44RX){
   ++n.mac44RXHardwareAttempts;const bool ok=configureMAC44RXHardware(io,checksum,n.rxChecksumHardware,n.mac44RXHardware);
   if(n.mac44RXHardware.status!=MAC44RXHardwareStatus::Ok)++n.mac44RXHardwareFailures;
   n.rxChecksumActive=checksum&&n.rxChecksumHardware.verified;return ok;
  }
  auto &r=n.rxChecksumHardware;r.attempted=true;
  if(!io.read16(0xe0,cplus)||(n.macVersion==42&&cplus==0xffff))return false;
  r.written=uint16_t((n.macVersion==34?cplus&0x2003U:cplus&~0x60U)|(checksum?0x20U:0));
  if(!io.write16(0xe0,r.written))return false;
  r.readValid=io.read16(0xe0,r.read);r.verified=r.readValid&&r.read!=0xffff&&(r.read&0x60U)==(checksum?0x20U:0U);
  n.rxChecksumActive=r.verified&&checksum;return r.verified;
 };
 const uint32_t rxConfig=(n.macVersion==34?RxConfig&~(1U<<11):RxConfig)|(promisc?1:0);
 if(!writeVerify(io,0x20,uint32_t(tx))||!writeVerify(io,0x24,uint32_t(tx>>32))||
    !writeVerify(io,0xe4,uint32_t(rx))||!writeVerify(io,0xe8,uint32_t(rx>>32))||
    !configureRX()||!io.write16(0xe2,0)||!io.write16(0xda,uint16_t(n.bufferSize+1))||!io.read16(0xda,size)||size!=n.bufferSize+1||
    !io.write32(8,0xffffffff)||!io.write32(12,0xffffffff)||
    !io.write32(0x44,rxConfig)||!io.read32(0x44,config)||
    (config&0xcf3f)!=rxConfig||
    !io.write8(0xec,0x27)||!io.read8(0xec,maxPacket)||maxPacket!=0x27||
    !io.write32(0x40,(3U<<24)|(7U<<8)|(1U<<7))||!io.write16(0x3e,0xffff)){
  n.status=NetworkStatus::IO;return false;
 }
 if(d.setBusMaster(true)!=DMAStatus::Ok){n.status=NetworkStatus::IO;return false;}
 a.busMasterEnabled=true;
 // From this point a failed enable also requires stopNetwork before resources can be released.
 n.running=true;
 if(!io.write8(0x37,12)||!io.read8(0x37,command)||command!=12){n.status=NetworkStatus::IO;return false;}
 n.status=NetworkStatus::Ok;return true;
}
NetworkStatus submitNetwork(RegisterIO &io,DMAProvider &d,Stage2AResult &a,NetworkResult &n,DMABuffer &packet,void *cookie,uint64_t traceID,const TXChecksumPlan &checksum,const TSO4HardwarePlan &tso,const TSO6HardwarePlan &tso6){
 if(!n.running||n.status!=NetworkStatus::Ok)return NetworkStatus::NotReady;
 if(n.macVersion==34&&(tso.enabled()||tso6.enabled()))return NetworkStatus::ChecksumRequest; // EVL errata: software TSO only.
 if(n.macVersion==34&&(checksum.mode()==TXChecksumMode::HardwareIPv4||checksum.mode()==TXChecksumMode::HardwareIPv6)&&
    (!n.mac34TXAuthorized||!n.mac34Native64Verified||!d.dmaReady()||d.deviceMapperPresent()))return NetworkStatus::ChecksumRequest;
 if(n.macVersion==46&&(checksum.mode()==TXChecksumMode::HardwareIPv4||checksum.mode()==TXChecksumMode::HardwareIPv6)&&
    (!n.mac46TXAuthorized||!mac46DMAAuthorized(d,n)))return NetworkStatus::ChecksumRequest;
 const unsigned count=unsigned(packet.segmentCount);
 const bool segmented=tso.enabled()||tso6.enabled();
 if(tso.enabled()&&tso6.enabled())return NetworkStatus::ChecksumRequest;
 if(tso.mac44()&&(!n.mac44TSO4Authorized||!d.dmaReady()||(!d.deviceMapperPresent()&&!n.mac44Native64ModeVerified)||
    packet.requestedLength>uint64_t(n.mac44TSO4Limit)+14U))return NetworkStatus::ChecksumRequest;
 if(tso6.mac44()&&(!n.mac44TSO6Authorized||!d.dmaReady()||(!d.deviceMapperPresent()&&!n.mac44Native64ModeVerified)||
    packet.requestedLength>uint64_t(n.mac44TSO6Limit)+14U))return NetworkStatus::ChecksumRequest;
 if(tso.mac46()&&(!n.mac46TSO4Authorized||!mac46DMAAuthorized(d,n)||
    !n.mac46TSO4Limit||n.mac46TSO4Limit>9000||packet.requestedLength>uint64_t(n.mac46TSO4Limit)+14U))return NetworkStatus::ChecksumRequest;
 if(tso6.mac46()&&(!n.mac46TSO6Authorized||!mac46DMAAuthorized(d,n)||
    !n.mac46TSO6Limit||n.mac46TSO6Limit>9000||packet.requestedLength>uint64_t(n.mac46TSO6Limit)+14U))return NetworkStatus::ChecksumRequest;
 if(segmented&&(count!=1||checksum.mode()!=TXChecksumMode::None||
    (tso.enabled()?!tso.validFor(static_cast<const uint8_t*>(d.bytes(packet)),size_t(packet.requestedLength),n.macVersion,n.mtu):
                    !tso6.validFor(static_cast<const uint8_t*>(d.bytes(packet)),size_t(packet.requestedLength),n.macVersion,n.mtu))))return NetworkStatus::ChecksumRequest;
 if(!cookie||!packet.mapped||!count||count>4||packet.requestedLength<60||(!segmented&&packet.requestedLength>maxFrameBytes(n.mtu))||
    packet.mappedLength!=packet.requestedLength||
    (!segmented&&!frameLengthValid(static_cast<const uint8_t*>(d.bytes(packet)),size_t(packet.requestedLength),n.mtu)))return NetworkStatus::InvalidPacket;
 uint64_t length=0;
 for(unsigned i=0;i<count;++i){auto s=packet.segments[i];
  if(!s.address||!s.length||s.length>0x3fff||s.address>UINT64_MAX-(s.length-1))return NetworkStatus::InvalidPacket;
  length+=s.length;
 }
 if(length!=packet.requestedLength)return NetworkStatus::InvalidPacket;
 // First hardware checksum path is restricted to one contiguous private snapshot.
 // Multiple segments use completed software bytes; do not extrapolate SG opts2 semantics.
 if((checksum.mode()==TXChecksumMode::HardwareIPv4||checksum.mode()==TXChecksumMode::HardwareIPv6)&&count!=1)return NetworkStatus::ChecksumRequest;
 if(checksum.mode()!=TXChecksumMode::None&&!checksum.validFor(static_cast<const uint8_t*>(d.bytes(packet)),size_t(packet.requestedLength),n.macVersion))return NetworkStatus::ChecksumRequest;
 if(count>n.txCount-n.used){n.txFullReason=2;n.txFullRequested=count;return NetworkStatus::Full;}
 for(unsigned j=0;j<count;++j){unsigned i=(n.producer+j)%n.txCount;
  const uint32_t observed=n.tx[i*4];
  if(observed&Own){n.txFullReason=3;n.txFullIndex=i;n.txFullWord=observed;n.txFullRequested=count;return NetworkStatus::Full;}
 }
 if(jtxSync(d,packet,n,traceID,false)!=DMAStatus::Ok)return NetworkStatus::Sync;
 // Publish fragments backwards; the first fragment's OWN is released last, as in r8169.
 for(unsigned j=0;j<count;++j){unsigned i=(n.producer+j)%n.txCount;auto s=packet.segments[j];
  n.tx[4*i+2]=uint32_t(s.address);n.tx[4*i+3]=uint32_t(s.address>>32);n.tx[4*i+1]=j==0?(segmented?(tso6.enabled()?tso6.options2():tso.options2()):checksum.descriptorOptions()):0;
  n.tx[4*i]=uint32_t(s.length)|(segmented?(tso6.enabled()?tso6.options1():tso.options1()):0)|end(i,n.txCount)|(j==0?First:0)|(j+1==count?Last:0);
 }
 const unsigned last=(n.producer+count-1)%n.txCount;
 n.traceIDs[last]=traceID;
 if(n.trace)n.trace->publication(traceID,n.producer,last,count,n.tx[4*n.producer]|Own,n.tx[4*n.producer+1],n.used+count);
 n.txPackets[last]=packet;n.cookies[last]=cookie;packet=DMABuffer{};
 n.used+=count;++n.txSubmitted;
 d.memoryBarrier();
 for(unsigned j=count;j>1;--j)n.tx[4*((n.producer+j-1)%n.txCount)]|=Own;
 d.memoryBarrier();n.tx[4*n.producer]|=Own;
 n.producer=(n.producer+count)%n.txCount;d.memoryBarrier();
 if(jtxSync(d,a.tx,n,traceID,true)!=DMAStatus::Ok||!jtxDoorbell(io,n,traceID)){
  n.status=NetworkStatus::IO;return n.status; // Packet ownership was already transferred.
 }
 return NetworkStatus::Ok;
}
bool serviceNetwork(RegisterIO &io,DMAProvider &d,Stage2AResult &a,NetworkResult &n,PacketSink &sink,uint16_t causes){
 if(!n.running)return false;
 if(n.trace)n.trace->service(causes);
 ++n.irqActions;if(causes&8)++n.txErrors;
 if(d.syncForCPU(a.tx)!=DMAStatus::Ok||d.syncForCPU(a.rx)!=DMAStatus::Ok){n.status=NetworkStatus::Sync;return false;}
 d.memoryBarrier();
 while(n.used){unsigned i=n.consumer;if(n.tx[4*i]&Own)break;
  if(n.cookies[i]){
   if(n.trace)n.trace->completion(n.traceIDs[i],n.tx[4*i],n.tx[4*i+1],causes);
   n.traceIDs[i]=0;cleanup(d,n.txPackets[i]);sink.releasePacket(n.cookies[i]);n.cookies[i]=nullptr;++n.txCompleted;}
  n.tx[4*i]=0;n.consumer=(i+1)%n.txCount;--n.used;
 }
 // Copy one bounded ring per IRQ; preserve hardware order across wraparound.
 for(unsigned budget=0;budget<kNetworkRXCount;++budget){unsigned i=n.rxConsumer;uint32_t opts=n.rx[4*i];
  if(opts&Own)break;
  const unsigned wireLength=opts&0x3fff;
  bool valid=!(opts&RxErrors)&&(opts&(First|Last))==(First|Last)&&wireLength>=18&&wireLength<=maxFrameBytes(n.mtu)+4&&
    wireLength<=n.rxBuffers[i].mappedLength&&wireLength<=n.rxBuffers[i].segments[0].length;
  if(valid){auto &b=n.rxBuffers[i];
   if(d.syncForCPU(b)!=DMAStatus::Ok){n.status=NetworkStatus::Sync;return false;}
   const auto *bytes=static_cast<const uint8_t*>(d.bytes(b));
   // Hardware includes the four-byte FCS; the network stack expects only the Ethernet frame.
   if(frameLengthValid(bytes,wireLength-4,n.mtu)){
    // Descriptor ring and bytes are CPU-synchronized. Snapshot opts2 BEFORE rearm;
    // decoding/publication completes synchronously before handing OWN back.
    const uint32_t opts2=n.rx[4*i+1];
    const auto metadata=decodeRXChecksum(bytes,wireLength-4,opts,opts2,n.rxChecksumActive,n.mtu,n.rxChecksum6Requested,n.rxChecksumRequested);
    if(n.rxChecksumProfile==RXChecksumProfile::MAC44RevisionCDeviceMapper&&n.macVersion==44){
     // Family labels only, not a second checksum parser; non-IP is not IPv4.
     if(bytes[12]==0x08&&bytes[13]==0x00)++n.mac44RXObserved4;
     else if(metadata.ipv6)++n.mac44RXObserved6;else ++n.mac44RXObservedOther;
    }
    n.rxChecksumStats.observe(metadata);
    if(sink.receive(bytes,wireLength-4,metadata))++n.rxDelivered;else ++n.rxDropped;
   }else ++n.rxDropped;
  }else ++n.rxDropped;
  if(d.syncForDevice(n.rxBuffers[i])!=DMAStatus::Ok){n.status=NetworkStatus::Sync;return false;}
  n.rx[4*i+1]=0;d.memoryBarrier();n.rx[4*i]=Own|end(i,kNetworkRXCount)|n.bufferSize;
  n.rxConsumer=(i+1)%kNetworkRXCount;
 }
 d.memoryBarrier();
 if(d.syncForDevice(a.rx)!=DMAStatus::Ok){n.status=NetworkStatus::Sync;return false;}
 // r8168 may lose a doorbell during closely spaced writes; re-kick remaining work after completion.
 if(n.used&&!jtxDoorbell(io,n,0)){n.status=NetworkStatus::IO;return false;}
 return true;
}
bool stopNetwork(RegisterIO &io,DMAProvider &d,Stage2AResult &a,NetworkResult &n,PacketSink &sink,PCIConfig *stopPCI){
 n.running=false;n.rxChecksumActive=false;
 // BME readback is mandatory, even if engines report disabled and descriptors still have OWN.
 DMAStatus bme=DMAStatus::InvalidState;
 if(n.stopSafety.enabled){
  StopSafetyReceipt r;
  r.irqAttempted=true;r.irqWritten=io.write16(0x3c,0);
  r.macAttempted=true;r.macWritten=io.write8(0x37,0);d.memoryBarrier();
  r.macReadAttempted=true;r.macReadReturned=io.read8(0x37,r.mac);
  r.macReadValid=r.macReadReturned&&r.mac!=0xff;r.macStopped=r.macReadValid&&!(r.mac&0x1c);
  if(stopPCI){r.identityReadReturned=stopPCI->readConfig32(0,r.identity);r.identityValid=r.identityReadReturned&&r.identity==0x816810ecU;
   r.beforeReadReturned=stopPCI->readConfig16(4,r.before);r.beforeValid=r.beforeReadReturned&&r.before!=0xffff;}
  // Containment is attempted even when a preceding operation failed.
  r.bmeAttempted=true;bme=d.setBusMaster(false);r.bmeStatus=uint32_t(bme);r.bmeReturnedOK=bme==DMAStatus::Ok;
  if(stopPCI){r.afterReadReturned=stopPCI->readConfig16(4,r.after);r.afterValid=r.afterReadReturned&&r.after!=0xffff;}
  r.bmeOffObserved=r.identityValid&&r.afterValid&&!(r.after&4);
  if(!r.irqWritten)r.failures|=StopSafetyReceipt::IRQWrite;
  if(!r.macWritten)r.failures|=StopSafetyReceipt::MACWrite;
  if(!r.macReadValid)r.failures|=StopSafetyReceipt::MACRead;
  else if(!r.macStopped)r.failures|=StopSafetyReceipt::MACActive;
  if(!r.identityValid)r.failures|=StopSafetyReceipt::PCIIdentity;
  if(!r.beforeValid)r.failures|=StopSafetyReceipt::PCIBefore;
  if(!r.afterValid)r.failures|=StopSafetyReceipt::PCIAfter;
  if(!r.bmeReturnedOK)r.failures|=StopSafetyReceipt::BMEBackend;
  if(r.afterValid&&(r.after&4))r.failures|=StopSafetyReceipt::BMEStillSet;
  n.stopSafety.record(r);
  if(r.bmeOffObserved&&r.bmeReturnedOK)a.busMasterEnabled=false;
  if(n.stopSafety.latched||a.stopSafetyRetained){
   a.stopSafetyRetained=true;n.unsafeRetained=true;n.status=NetworkStatus::UnsafeStop;
   // No logical cancellation, pool return, unmap, ring clear or completion.
   return false;
  }
 }else{
  io.write16(0x3c,0);io.write8(0x37,0);d.memoryBarrier();bme=d.setBusMaster(false);
 }
 if(bme!=DMAStatus::Ok){
  if(n.trace){n.trace->stopped(false);for(unsigned i=0;i<n.txCount;++i)
   if(n.cookies[i])n.trace->cancel(n.traceIDs[i],false,0,0);}
  n.unsafeRetained=true;n.status=NetworkStatus::UnsafeStop;return false;}
 a.busMasterEnabled=false;
 if(n.trace)n.trace->stopped(true);
 for(unsigned i=0;i<n.txCount;++i){
  if(n.trace&&n.cookies[i])n.trace->cancel(n.traceIDs[i],true,n.tx[4*i],n.tx[4*i+1]);
  n.traceIDs[i]=0;cleanup(d,n.txPackets[i]);if(n.cookies[i]){if(n.macVersion==34&&n.mac34Native64Verified)++n.mac34TXCancelled;sink.releasePacket(n.cookies[i]);}n.cookies[i]=nullptr;}
 n.used=0;n.unsafeRetained=false;
 if(n.tx)for(unsigned i=0;i<n.txCount*4;++i)n.tx[i]=0;
 if(n.rx)for(unsigned i=0;i<kNetworkRXCount*4;++i)n.rx[i]=0;
 d.memoryBarrier();
 if(a.tx.mapped)d.syncForDevice(a.tx);if(a.rx.mapped)d.syncForDevice(a.rx);
 return true;
}
bool releaseNetwork(RegisterIO &io,DMAProvider &d,Stage2AResult &a,NetworkResult &n,PacketSink &sink,PCIConfig *stopPCI){
 if(!stopNetwork(io,d,a,n,sink,stopPCI))return false;
 for(auto &b:n.rxBuffers)cleanup(d,b);
 n.prepared=false;n.tx=n.rx=nullptr;return true;
}
}
