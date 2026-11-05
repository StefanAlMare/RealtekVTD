// SPDX-License-Identifier: GPL-2.0-only
#include "../src/RegisterAccess.hpp"
#include <cassert>
#include <deque>
#include <vector>
#include <cstdio>
using namespace realtek_next;
struct Fake : RegisterIO {
    std::deque<uint32_t> reads;
    std::vector<uint32_t> writes;
    std::vector<uint16_t> offsets;
    unsigned delays = 0, readCount = 0;
    bool writeOK = true;
    bool read32(uint16_t, uint32_t &v) override {
        ++readCount;
        if (reads.empty()) return false;
        v=reads.front(); reads.pop_front(); return true;
    }
    bool write32(uint16_t o, uint32_t v) override {
        offsets.push_back(o); writes.push_back(v); return writeOK;
    }
    void delayUs(unsigned us) override { assert(us==25); delays += us; }
};
int main() {
    Fake f; RegisterAccess a(f);
    assert(a.macWrite(0xe63e,1)==AccessStatus::Ok);
    assert(f.offsets.back()==0xb0 && f.writes.back()==0xf31f0001U);
    f.reads={0x1234}; assert(a.macRead(0xc094).value==0x1234);
    assert(f.writes.back()==0x604a0000U);
    const auto n=f.writes.size();
    assert(a.phyWrite(0xa401,0)==AccessStatus::InvalidAddress);
    assert(a.macWrite(0x10000,0)==AccessStatus::InvalidAddress);
    assert(f.writes.size()==n);
    f.reads={0,0x80000001U,0x8000abcdU};
    auto r=a.mdioRead(1); assert(r.status==AccessStatus::Ok && r.value==0xabcd);
    assert(f.offsets.back()==0xb8 && f.writes.back()==0x52010000U && f.delays==25);
    f.reads={0x80000000U,0};
    assert(a.mdioWrite(0,0x1200)==AccessStatus::Ok);
    assert(f.writes.back()==0xd2001200U);
    const auto before=f.writes.size();
    assert(a.mdioWrite(31,0xa43)==AccessStatus::Ok);
    assert(f.writes.size()==before && a.mdioRead(31).value==0xa43);
    f.reads={0}; assert(a.mdioWrite(16,0x55)==AccessStatus::Ok);
    assert(f.writes.back()==0xd2180055U);
    assert(a.mdioWrite(31,0)==AccessStatus::Ok && a.mdioRead(31).value==0);
    f.reads=std::deque<uint32_t>(10,0); f.delays=0; f.readCount=0;
    r=a.phyRead(0xa400);
    assert(r.status==AccessStatus::Timeout && f.readCount==10 && f.delays==250);
    f.reads={0xffffffffU}; assert(a.phyRead(0xa400).status==AccessStatus::IOFailure);
    f.writeOK=false; const auto count=f.readCount;
    assert(a.phyWrite(0xa400,0)==AccessStatus::IOFailure && f.readCount==count);
    assert(a.mdioRead(32).status==AccessStatus::InvalidAddress);
    puts("PASS: OCP commands, MDIO pages, ordering, timeout and failure propagation");
}
