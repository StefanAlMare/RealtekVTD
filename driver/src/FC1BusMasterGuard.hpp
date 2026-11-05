// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "GU25FlowControl.hpp"
namespace realtek_next {
// BME1: establish the original FC1 precondition; never waive it.
// No DMA mapping/cleanup, PHY access, interrupt reconfiguration, or retry loop.
struct FC1BMEReceipt {
 bool enabled=false,unsafe=false,beforeRead=false,afterRead=false;
 bool pciBeforeValid=false,macBeforeValid=false,pciAfterValid=false,macAfterValid=false;
 bool disableAttempted=false,disableReturnedOK=false,verified=false;
 uint16_t pciBefore=0,pciAfter=0;
 uint8_t macBefore=0,macAfter=0;
 const char *outcome="not-observed";
};
// IO offers readPCI, readMAC, disableBME. disableBME must return OPERATION
// success (the project's DMAStatus==Ok), not IOPCIDevice's previous BME state.
// Caller holds the controller lock; caller may start PAUSE only after true.
template<class IO> bool fc1EstablishQuiescence(IO &io,bool enabled,bool unsafe,FC1BMEReceipt &r) {
 r=FC1BMEReceipt{};r.enabled=enabled;r.unsafe=unsafe;
 if(enabled||unsafe){r.outcome=enabled?"controller-enabled":"unsafe-retained";return false;}
 r.beforeRead=true;
 r.pciBeforeValid=io.readPCI(r.pciBefore);
 r.macBeforeValid=io.readMAC(r.macBefore);
 if(!r.pciBeforeValid||r.pciBefore==0xffff){r.outcome="pci-before-invalid";return false;}
 if(!r.macBeforeValid||r.macBefore==0xff){r.outcome="mac-before-invalid";return false;}
 // Only a valid BME-on / engines-OFF state is recoverable here.
 // Unexpected running engines are not stopped or ignored by this helper.
 if(r.macBefore&0x0cU){r.outcome="mac-not-stopped";return false;}
 if(!(r.pciBefore&4U)){
  r.verified=fc1QuiescentState(enabled,unsafe,true,r.pciBefore,true,r.macBefore);
  r.outcome="already-quiescent";return r.verified;
 }
 r.disableAttempted=true;
 r.disableReturnedOK=io.disableBME(); // exactly one existing checked operation
 // Retain actual readback even if the operation reported failure.
 r.afterRead=true;
 r.pciAfterValid=io.readPCI(r.pciAfter);
 r.macAfterValid=io.readMAC(r.macAfter);
 if(!r.disableReturnedOK){r.outcome="bme-disable-failed";return false;}
 if(!r.pciAfterValid||r.pciAfter==0xffff){r.outcome="pci-after-invalid";return false;}
 if(!r.macAfterValid||r.macAfter==0xff){r.outcome="mac-after-invalid";return false;}
 r.verified=fc1QuiescentState(enabled,unsafe,true,r.pciAfter,true,r.macAfter);
 r.outcome=r.verified?"bme-disabled-verified":
           (r.pciAfter&4U)?"bme-still-enabled":"mac-after-not-stopped";
 return r.verified;
}
}
