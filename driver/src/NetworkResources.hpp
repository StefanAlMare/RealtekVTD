// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "NetworkProgram.hpp"
#include "TXPool.hpp"
#include "JumboProfile.hpp"
namespace realtek_next {
struct NetworkResources {bool ready=false,jumboReady=false,jumboFallback=false;};
// Identity/copper eligibility is checked by jumboIdentityValid in the controller.
// These are secondary resource-shape checks, not an alternative chip identifier.
constexpr bool gu25JumboResourceShape(bool eligible,unsigned mac,unsigned txCount){
 return eligible&&mac==42&&txCount==kTXDescriptorMaxCount;
}
constexpr uint64_t kGU25JumboArenaBytes=uint64_t(kTXPoolMaxCount)*kJumboTXStride;
static_assert(kGU25JumboArenaBytes==4210688ULL,"bounded 257-slot Jumbo arena");
inline bool networkAllocationSizeAllowed(uint64_t size,uint32_t alignment,
 DMADirection direction,bool gu25JumboWindow){
 // The old general allocation bound remains. The only extension is ONE exact
 // TX arena shape during authorized ungated network-resource preparation.
 return size<=uint64_t(kTXPoolCount)*kJumboTXStride||
  (gu25JumboWindow&&size==kGU25JumboArenaBytes&&
   alignment==kDescriptorRingAlignment&&direction==DMADirection::ToDevice);
}
// Ungated startup only. No replacement while a producer can access the pool.
inline NetworkResources prepareNetworkResources(DMAProvider &dma,Stage2AResult &rings,
 NetworkResult &network,TXPool &pool,bool allowJumbo,bool allowGU25Jumbo=false){
 NetworkResources result;
 const bool extended=allowGU25Jumbo&&gu25JumboResourceShape(allowJumbo,network.macVersion,rings.txDescriptorCount);
 if((allowGU25Jumbo&&!extended)||(rings.txDescriptorCount==kTXDescriptorMaxCount&&allowJumbo&&!extended)||network.running||network.prepared||network.unsafeRetained||pool.ready()||pool.leased()||
    !rings.complete||dma.setBusMaster(false)!=DMAStatus::Ok)return result;
 rings.busMasterEnabled=false; // runStage2A leaves stopped engines with BME on.
 if(allowJumbo){
  if(prepareNetwork(dma,rings,network,kJumboRXBytes)&&pool.initialize(dma,kJumboTXStride,network.txCount+1,extended)){
   result.ready=result.jumboReady=true;return result;
  }
  result.jumboFallback=true;
  if(!discardPreparedNetwork(dma,rings,network)||!pool.shutdown(dma))return result;
 }
 result.ready=prepareNetwork(dma,rings,network,kNetworkBufferSize)&&pool.initialize(dma,kTXPoolStride,network.txCount+1);
 if(!result.ready){discardPreparedNetwork(dma,rings,network);pool.shutdown(dma);}
 return result;
}
}
