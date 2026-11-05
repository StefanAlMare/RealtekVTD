// SPDX-License-Identifier: GPL-2.0-only
#include "TXSnapshot.hpp"
#include "FramePolicy.hpp"
#include "JumboProfile.hpp"
namespace realtek_next {
namespace {
DMAStatus discard(DMAProvider &dma,DMABuffer &buffer,DMAStatus status){
 if(buffer.mapped)dma.unmap(buffer);
 if(buffer.prepared)dma.complete(buffer);
 if(buffer.allocated)dma.release(buffer);
 return status;
}
}
DMAStatus prepareTXSnapshot(DMAProvider &dma,DMABuffer &buffer,size_t length,TXCopy copy,void *context){
 if(buffer.opaque||buffer.allocated||!copy||length<60||length>maxFrameBytes(kJumboMTU))return DMAStatus::InvalidState;
 if(!dma.dmaReady())return DMAStatus::MapperMissing;
 auto status=dma.allocate(buffer,length,kDescriptorRingAlignment,DMADirection::ToDevice);
 if(status!=DMAStatus::Ok)return discard(dma,buffer,status);
 void *destination=dma.bytes(buffer);
 if(!destination||!copy(context,destination,length)||
    !frameLengthValid(static_cast<const uint8_t*>(destination),length,kJumboMTU))return discard(dma,buffer,DMAStatus::InvalidState);
 status=dma.prepare(buffer);if(status!=DMAStatus::Ok)return discard(dma,buffer,status);
 status=dma.map(buffer);if(status!=DMAStatus::Ok)return discard(dma,buffer,status);
 if(buffer.segmentCount!=1)return discard(dma,buffer,DMAStatus::SegmentCountInvalid);
 const auto segment=buffer.segments[0];
 if(buffer.mappedLength!=length||segment.length!=length||!segment.address)
  return discard(dma,buffer,DMAStatus::MapFailed);
 if(segment.address>UINT64_MAX-(length-1))return discard(dma,buffer,DMAStatus::RangeOverflow);
 return DMAStatus::Ok;
}
}
