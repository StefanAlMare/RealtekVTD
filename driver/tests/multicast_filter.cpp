// SPDX-License-Identifier: GPL-2.0-only
#include "../src/MulticastFilter.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
using namespace realtek_next;
uint32_t oracle(const uint8_t *a){
 uint32_t reflected=0xffffffffU;
 for(unsigned i=0;i<6;++i){reflected^=a[i];for(unsigned j=0;j<8;++j)reflected=(reflected>>1)^((reflected&1)?0xedb88320U:0);}
 uint32_t result=0;for(unsigned i=0;i<32;++i){result=(result<<1)|(reflected&1);reflected>>=1;}return result;
}
struct IO:RegisterIO {
 std::map<unsigned,uint32_t> regs{{0x44,0x1234c70e},{8,0xffffffffU},{12,0xffffffffU}};
 std::vector<unsigned> writes;unsigned calls=0,fail=0;bool gone=false,corrupt=false;
 bool read32(uint16_t o,uint32_t &v)override{assert(o==8||o==12||o==0x44);if(++calls==fail)return false;v=gone?0xffffffffU:regs[o];if(corrupt&&o==8)v^=1;return true;}
 bool write32(uint16_t o,uint32_t v)override{assert(o==8||o==12||o==0x44);writes.push_back(o);if(++calls==fail||gone)return false;regs[o]=v;return true;}
 void delayUs(unsigned)override{assert(false);}
};
int main(){
 uint8_t a[6]={1,0,0x5e,0,0,0xfb},b[6]={0x33,0x33,0,0,0,0xfb};
 uint32_t state=1234567;unsigned bins[64]{};
 for(unsigned n=0;n<10000;++n){uint8_t p[6];for(auto &v:p){state=state*1664525U+1013904223U;v=uint8_t(state>>24);}p[0]|=1;assert(multicastCRC(p)==oracle(p));++bins[multicastCRC(p)>>26];}
 for(auto n:bins)assert(n);
 for(unsigned mac:{44U,46U})for(bool enabled:{false,true})for(bool promisc:{false,true}){
  MulticastFilter f;IO io;assert(f.list(a,1));f.enabled=enabled;const auto high=io.regs[0x44]&~15U;assert(f.apply(io,promisc,mac));
  uint64_t expected=promisc?UINT64_MAX:enabled?uint64_t(1)<<(oracle(a)>>26):0;
  assert(io.regs[8]==multicastSwap(uint32_t(expected>>32)));assert(io.regs[12]==multicastSwap(uint32_t(expected)));
  assert((io.regs[0x44]&~15U)==high);assert((io.regs[0x44]&15)==(0x0aU|(promisc?5U:enabled?4U:0U)));
  assert(f.valid&&!f.fallback&&f.readValid==7&&f.appliedGeneration==f.generation);
  f.invalidate();assert(!f.valid&&f.mask==(uint64_t(1)<<(oracle(a)>>26)));assert(f.apply(io,promisc,mac));
 }
 MulticastFilter f;assert(f.list(a,1));const auto saved=f.mask;assert(!f.list(nullptr,1)&&f.mask==saved);
 uint8_t unicast[6]{};assert(!f.list(unicast,1)&&f.mask==saved);assert(f.list(nullptr,0)&&!f.mask&&!f.addressCount);
 uint8_t pair[12];for(unsigned i=0;i<6;++i)pair[i]=pair[i+6]=a[i];assert(f.list(pair,2)&&f.mask==saved&&f.addressCount==2);
 assert(f.list(b,1));f.enabled=true;IO mode;assert(f.apply(mode,true,44));assert(f.apply(mode,false,44));assert(mode.regs[8]==multicastSwap(uint32_t(f.mask>>32)));
 for(unsigned fail=1;fail<=7;++fail){MulticastFilter t;t.enabled=true;assert(t.list(a,1));IO io;io.fail=fail;
  assert(t.apply(io,false,44));assert(t.valid&&t.fallback&&t.applied==MulticastApplied::Fallback);assert(io.regs[8]==UINT32_MAX&&io.regs[12]==UINT32_MAX&&(io.regs[0x44]&15)==14);
 }
 IO bad;bad.corrupt=true;assert(!f.apply(bad,false,46)&&!f.valid&&f.fallback);
 IO gone;gone.gone=true;assert(!f.apply(gone,false,46)&&!f.valid);
 IO unsupported;assert(!f.apply(unsupported,false,35)&&unsupported.writes.empty());
 // Collision must be represented by one hash bit, not treated as perfect filtering.
 uint8_t collision[6]={1,0,0x5e,0,1,0};bool found=false;
 for(unsigned i=0;i<256&&!found;++i){collision[5]=uint8_t(i);found=oracle(collision)>>26==oracle(a)>>26;}
 assert(found);for(unsigned i=0;i<6;++i){pair[i]=a[i];pair[6+i]=collision[i];}assert(f.list(pair,2)&&f.mask==saved);
 std::puts("PASS: multicast CRC oracle 10000 addresses/all 64 bins, MAC44/46 modes, restore, collisions, invalid lists and verified fallback failures");
}
