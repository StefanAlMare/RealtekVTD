# RealtekVTDNext — progres și matrice de teste

Obiectivul imediat este acceptanța 0.20.3-MEDIA1 pentru raportarea PAUSE IEEE 802.3x. Inventarul celorlalte capabilități pregătește etape viitoare; MEDIA1 nu le activează și nu schimbă negocierea, DMA, IRQ, IOMMU, firmware sau EFI.

## Starea verificată

| Etapă | Rezultat | Dovadă / limită |
|---|---|---|
| Recuperare baseline exact | PASS | ZIP 0.20.2 și executabil cu hash-urile canonice; manifest 137/137; folder identic cu ZIP |
| Recompilare baseline | PASS | Xcode local, executabil identic bit cu bit cu fallback-ul validat |
| Scope MEDIA1 | PASS | Toate sursele hardware existente și backend-ul neschimbate; în adapter numai media/raportare și versiune |
| Teste offline și build MEDIA1 | PASS | Suite baseline/candidat, matrice PAUSE, failure injection, audit kext x86_64 |
| PAUSE efectiv pe hardware | PENDING | Nicio instalare/încărcare făcută în această sesiune |
| Alte capabilități hardware | INVENTAR / PENDING | Se confirmă separat pe fiecare placă; nu se deduc din numele familiei |

Captura istorică ASUS `autoneg-20261006-023959.tgz` a fost recuperată și hash-ul verificat: `56832765a83428ade55d3fecb2e5744e1b9683db429b3d790ad3982734f6151f`. Arată 0.20.2, 1000 Mb/s full-duplex, ANAR `0x0de1`, ANLPAR `0xcde1`, PHYstatus `0xf3`, AppleVTD/device mapper prezent și networking funcțional. PAUSE simetric este indicat de negociere și PHYstatus; emiterea/respectarea fizică a cadrelor PAUSE nu a fost măsurată.

Handover-ul din 6 octombrie conține teste RTL8168H pe **Acer**, subsystem `1025:100c`. Acestea nu validează placa **Dell**. Pentru Dell se păstrează separat subsystem, revision, XID, MAC_VER, PHY IDs, OS/build și rezultate. Nu se copiază identitatea Acer în fișa Dell.

## Cele patru celule independente

| Celulă | Hardware țintă | AppleVTD | Baseline 0.20.2 | Candidat 0.20.3 |
|---|---|---|---|---|
| A+ | ASUS RTL8411B; PCI 10ec:8168, subsystem 1043:200f, rev 0x12, XID 0x5c8, MAC_VER_44 | activ | Captură istorică de link/network; banc PAUSE PENDING | TOT PENDING |
| A− | Același ASUS | inactiv | Funcționare raportată de utilizator; comparație controlată PENDING | TOT PENDING |
| D+ | Dell RTL8168H; așteptat XID 0x541/MAC_VER_46, identitate placă de confirmat | activ | PENDING | TOT PENDING |
| D− | Același Dell | inactiv | PENDING | TOT PENDING |

Fiecare celulă cuprinde două rulări comparabile: fallback-ul 0.20.2 și candidatul. Deployment-ul și alegerea modului la boot aparțin utilizatorului. Un rezultat nu se transferă între calculatoare sau moduri.

Modul activ se confirmă prin `DMAMode=device-mapper-native64`, `DeviceMapperPresent=true`, legătura mapper-ului cu dispozitivul și evidența AppleVTD din IORegistry. Modul inactiv cere `no-iommu-native64`, mapper absent, provider fără iommu-parent și lipsa AppleVTD activ în colectarea relevantă. Un boot argument sau absența unei linii într-o comandă eșuată nu este suficient. Orice eroare de colectare se marchează UNKNOWN.

## Fișa fiecărei rulări

Se păstrează: celulă, dată/ore, hardware complet, OS/build, kext version/UUID/SHA-256, interfața enX aparținând driverului, adresă IP, peer/port/switch/firmware, cablu, MTU, viteză/duplex, ANAR/ANLPAR, starea EEE, modul DMA, index/tip interrupt, sleep/wake și condiția de trafic. Se salvează înainte/după contoarele driverului și portului peer, capturile brute și jurnalul testerului. Nu se publică doar o fotografie din Settings.

Rezultate permise: PASS, FAIL, INCONCLUSIVE, NOT RUN, UNSUPPORTED sau UNREACHABLE WITH BASELINE. O capabilitate presupusă, o probă lipsă ori un contor indisponibil nu devin PASS.

## Protocol MEDIA1 în fiecare celulă

1. Verifică hash-urile arhivei și executabilului ales. Păstrează fallback-ul original separat. Confirmă după boot versiunea realmente încărcată și identitatea dispozitivului.
2. După stabilizarea link-ului, colectează de trei ori, la minimum două secunde distanță, cu `TEST_MEDIA1.command --machine asus|dell --vtd on|off`, pentru candidatul instalat manual. Comanda nu instalează și nu generează trafic. Pentru baseline folosește collectorul său original și probele de banc comparabile.
3. Confirmă auto selectat, active medium din dicționar, link status/speed, duplex și diagnosticele PAUSE. În IORegistry numele mediei este un șir hexazecimal, de exemplu `00500030` pentru 1000/full/FLOW; se verifică obiectul `Type/Speed/Index` din IOMediumDictionary, nu o căutare textuală după „flow-control”.
4. Rulează cazurile de negociere de mai jos modificând numai partenerul de banc. Citește advertising-ul real la ambele capete. Păstrează advertising-ul DUT exact ca în baseline.
5. Măsoară RX PAUSE și TX PAUSE separat, cu probe externe. Apoi execută regresiile de trafic și lifecycle.
6. Compară cu baseline în aceeași celulă. Salvează verdictul separat pentru raportare, negociere, RX PAUSE efectiv, TX PAUSE efectiv și regresii.

Observația nouă folosește callback-ul existent de o secundă. Tranzițiile mai scurte decât intervalul pot lipsi dintr-o captură; aceasta nu este telemetrie continuă sau un snapshot atomic al tuturor registrelor. Citirea PHY are timeout finit; PAUSE nu se consideră valid la citiri incomplete/0xffff, reset/restart, forced mode, half-duplex, autoneg incomplet sau schimbarea PHYstatus în timpul observației.

| Test | Proba și criteriul | A+ | A− | D+ | D− |
|---|---|---|---|---|---|
| M01 Media publicată | 7 medii baseline + 3 full/FLOW, toate unice; auto rămâne selectat | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| M02 PAUSE simetric | AN complet/valid, TX/RX negociat true; PHYstatus concordant; FLOW în media activă | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| M03 Fără PAUSE | TX/RX false, stare validă none, media fără FLOW | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| M04 PAUSE asimetric | Direcție corectă în diagnostice; media generică fără FLOW; doar cazurile accesibile cu advertising baseline | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| M05 Citire invalidă / forced / restart | Fake tests PASS; probe hardware doar unde starea poate fi obținută fără modificarea driverului | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| M06 Link down/up | 30 cicluri cu intervale >2 s; fără FLOW sau media activă rămasă după link down; renegociere corectă | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| M07 Viteze/duplex | 10/100/1000 full, 10/100 half dacă accesibile prin peer; half nu primește PAUSE negociat/FLOW | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| M08 RX PAUSE efectiv | Cadre control primite și suspendarea TX de date a DUT, cu relație temporală măsurată | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| M09 TX PAUSE efectiv | Cadre emise de DUT sub presiune RX și răspunsul partenerului, fără schimbarea DMA/pragurilor | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| M10 Trafic normal | TCP TX/RX/bidirecțional, UDP, latență, erori și completări comparate cu baseline | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| M11 Lifecycle | 5 cold boots, 10 warm boots, 10 disable/enable, 10 sleep/wake; fără stare PAUSE veche | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| M12 Soak | 8 ore după trecerea probelor scurte; include perioade idle și trafic bidirecțional | NOT RUN | NOT RUN | NOT RUN | NOT RUN |

## Matricea PAUSE — perspectiva DUT

P = Pause bit (0x0400), A = Asym_Pause bit (0x0800). TX înseamnă că DUT poate emite PAUSE; RX că DUT poate respecta PAUSE primit. Bitul de advertising nu demonstrează efectul fizic.

| Advertising DUT ↓ / peer → | 0 | P | A | P+A |
|---|---|---|---|---|
| 0 | none | none | none | none |
| P | none | TX+RX | none | TX+RX |
| A | none | none | none | TX |
| P+A | none | TX+RX | RX | TX+RX |

Rezoluția urmează [tabelul 28B-3, implementarea de referință Linux](https://kernel.googlesource.com/pub/scm/linux/kernel/git/bpf/bpf-next/+/169e77764adc041b1dacba84ea90516a895d43b2/include/linux/mii.h). Toate cele 16 cazuri sunt testate offline. ASUS-ul istoric promovează P+A: cu advertising-ul local păstrat, se pot exercita none, symmetric și RX-only prin schimbarea peer-ului. **TX-only este inaccesibil pe acel baseline ASUS**; se marchează UNREACHABLE WITH BASELINE și nu se schimbă ANAR pentru a-l forța. Pe Dell se citește întâi advertising-ul real, apoi se aleg coloanele accesibile. Starea forced/local autoneg disabled se verifică offline dacă nu poate apărea în fluxul nemodificat.

`PauseTxNegotiated/PauseRxNegotiated` sunt valabile numai cu `PauseNegotiatedValid=true`. `PausePHYStatusTx/Rx` sunt indicații ale registrului PHYstatus și au validitate separată. Un dezacord este păstrat vizibil, iar FLOW nu este publicat; nu se „corectează” configurarea hardware. `PauseEffectiveVerified=false` nu înseamnă că flow control este oprit, ci că driverul nu poate certifica măsurătoarea de banc.

## Măsurarea PAUSE efectiv

Se folosește un peer/tester capabil să controleze PAUSE, un TAP care păstrează cadrele MAC control sau contoare documentate pe portul switch-ului orientat spre DUT. Un mirror/SPAN obișnuit poate omite cadrele link-local; se verifică întâi că metoda vede cadre control cunoscute. Captura pe host poate să nu vadă cadre consumate/generate de MAC. `tcpdump`, ping și throughput singure nu dovedesc 802.3x.

Cadrele urmărite au EtherType 0x8808, opcode PAUSE 0x0001 și destinația standard 01:80:c2:00:00:01. Se diferențiază PAUSE MAC control de „zero window” TCP, congestie TCP și PFC 802.1Qbb. Nu se adaugă suport PFC în MEDIA1. [IEEE — adresa rezervată și operația PAUSE](https://www.ieee802.org/3/maint/public/grow_1_0118.pdf).

**RX PAUSE:** în timp ce DUT transmite date continuu spre peer, testerul trimite PAUSE valid spre DUT. Se înregistrează timpul și quanta, creșterea contorului peer-port TxPause/DUT RxPause și reducerea temporară a TX de date al DUT, apoi reluarea. Durata nominală este `quanta × 512 / link_bps`, cu toleranța/întârzierea și cadrele deja în zbor caracterizate de banc. [IEEE — unitatea pause_quanta](https://www.ieee802.org/3/10G_study/email/save/msg04772.html). Se încearcă quanta 128, 1024 și 8192, individual, plus quanta zero pentru reluare. Nu se prelungesc artificial suspendările până la declanșarea watchdog-ului existent. Control negativ: caz none sau half-duplex accesibil; se păstrează toate rezultatele, inclusiv comportament hardware neașteptat.

**TX PAUSE:** testerul produce presiune reală pe recepția DUT folosind rata/pattern-ul de pe banc, fără modificarea driverului, ringurilor DMA sau pragurilor FIFO. Se cer cadre PAUSE emise de DUT, creșterea contorului peer-port RxPause/DUT TxPause și reacția TX de date a peer-ului. Un iperf RX fără PAUSE observat nu dovedește că funcția lipsește: poate să nu umple FIFO-ul. Dacă bancul nu poate provoca și observa condiția, rezultatul este INCONCLUSIVE, nu FAIL sau PASS inventat.

Contoarele sunt etichetate după capăt: **DUT TxPause = peer-port RxPause**, **DUT RxPause = peer-port TxPause**. Se salvează valorile înainte/după, nu numai totaluri istorice. [Cisco — PAUSE, congestie și contoare RxPause/TxPause](https://www.cisco.com/c/en/us/td/docs/switches/lan/c9000/lyr2-fwd/flowcontrol-stormcontrol/flow-control-and-storm-control-configuration-guide/configure-flow-control.html). RX pause oprește TX de date al DUT; TX pause solicită peer-ului să oprească TX de date către DUT.

## Regresii și criterii de acceptare

- Aceeași interfață cablată, același peer/port/cablu/MTU și același advertising pentru perechea baseline/candidat. Ping-ul și traficul trebuie legate de interfața/adresa DUT, fără succes accidental prin Wi-Fi.
- Trei repetări de câte 120 s TCP TX, RX și bidirecțional, fără injectare PAUSE; UDP cu pachete mici și MTU 1500, la rate sub capacitatea măsurată a baseline-ului. Se salvează throughput, retransmisii, pierderi, distribuția latenței și contoarele TX/RX/IRQ.
- Țintă preliminară: throughput median la maximum 5% sub baseline și p99 de latență la maximum 10% peste baseline în condiții comparabile. Dacă variația bancului depășește pragurile, se repetă perechea și se marchează INCONCLUSIVE. Scăderea intenționată în timpul PAUSE nu se compară cu acest prag.
- Fără panic, reset necerut, fault mapper/IOMMU, watchdog nou, resurse DMA reținute nesigur sau link drop neexplicat. După liniștirea traficului, TX pending/pool leased trebuie să se golească și completările să progreseze; diferența submitted/completed în trafic simultan nu este singură dovadă de stall.
- La disable/sleep/link down se cere PAUSE invalid, FLOW absent, media inactivă. După revenire se cere observație proaspătă și noul rezultat al negocierii, fără valori vechi. Se păstrează aceeași politică PM/EEE/IRQ/DMA ca baseline.
- Un defect care implică configurare hardware, DMA sau autoneg se raportează separat. MEDIA1 nu se extinde pentru a-l repara. Utilizatorul decide revenirea manuală la fallback.

Acceptanța completă cere rezultate explicite în A+, A−, D+, D− și dovezi pentru RX/TX PAUSE efectiv acolo unde negocierea permite. Un PASS local, un snapshot coerent ori un rezultat ASUS nu validează Dell sau celălalt mod AppleVTD.

## Inventarul tuturor capabilităților — etape ulterioare

P = de inventariat/testat separat; N/A se atribuie numai după verificarea exactă a cipului, plăcii și interfeței software. Referința Realtek pentru familia RTL8111H nu este o fișă exhaustivă pentru orice RTL8168H sau RTL8411B. Fiecare rând cere capabilitate hardware confirmată, suport publicat de driver și probă efectivă, separat.

| Capabilitate / verificare | Situație în sursa baseline / probă necesară | A+ | A− | D+ | D− |
|---|---|---|---|---|---|
| Identitate PCI/XID/PHY/revizie | Profil specific; nu doar PCI 10ec:8168 | P | P | P | P |
| Autoneg 10/100/1000 full | Framework existent păstrat; peer capabil și capabilități PHY reale | P | P | P | P |
| Half-duplex 10/100 | Test de peer și contori collision/defer dacă disponibili; 1000 half numai dacă PHY îl raportează | P | P | P | P |
| Auto-MDI/MDIX / cablu | Inventar exact și cabluri/peer controlate | P | P | P | P |
| IEEE 802.3x PAUSE | Singura etapă nouă MEDIA1; negociere, raportare, RX/TX fizic | P | P | P | P |
| MTU 1500 / dimensiuni limită | TX existent limitează lungimea la 1514; fără extindere în MEDIA1 | P | P | P | P |
| Jumbo frames | Neimplementate ca opțiune MEDIA1; confirmare hardware și proiect separat înainte de activare | P | P | P | P |
| VLAN 802.1Q / priority tags | Tag software, MTU și filtre/offload de inventariat separat | P | P | P | P |
| Broadcast / multicast / promiscuous | Promiscuous existent; callback-urile multicast nu programează o listă; filtrare efectivă de verificat | P | P | P | P |
| Checksum RX/TX IPv4, TCP, UDP | Capabilități și offload nepublicate explicit în acest adapter; packet capture/verificare separată | P | P | P | P |
| Checksum RX/TX IPv6, TCP, UDP | Aceleași verificări, incluzând extension headers și fallback software | P | P | P | P |
| TSO IPv4/IPv6 | Nu se adaugă în MEDIA1; publicare și segmentare pe fir înainte de revendicare | P | P | P | P |
| Scatter/gather și mbuf chains | Snapshot/pool TX existent păstrat; lanțuri, dimensiuni și erori de mapare | P | P | P | P |
| DMA 64-bit / presiune memorie | Regresie asupra ambelor căi existente, fragmentare și limite; fără modificarea alocării | P | P | P | P |
| MSI/MSI-X / interrupt moderation | Se înregistrează vectorul selectat efectiv; multiple vectors/moderation neactivate aici | P | P | P | P |
| RSS / multiqueue | Nu se presupun pe aceste două plăci; sursa folosește calea de coadă existentă | P | P | P | P |
| EEE / Green Ethernet | Politica MAC_VER_46 rămâne distinctă; confirmare advertising/LPI și latență pe fiecare placă | P | P | P | P |
| PCIe ASPM/LTR / consum | Confirmare platformă, residency/consum și efect asupra link-ului; proiect separat | P | P | P | P |
| D0/D3, disable/enable, sleep/wake | Lifecycle existent păstrat; trafic și raportare după fiecare tranziție | P | P | P | P |
| WoL magic/pattern/link | Suport placă, alimentare, firmware/OS și driver de inventariat; fără configurare EFI aici | P | P | P | P |
| ARP/NS sleep offload / ECMA | Se confirmă numai dacă hardware și OS/driver expun funcția | P | P | P | P |
| CRC, alignment, runt/oversize, drops | Contoare hardware documentate și cadre de tester, comparate cu statisticile driverului | P | P | P | P |
| Link renegotiation / cablu defect / peer reset | Viteză/duplex, erori, recuperare și continuitate | P | P | P | P |
| LED, cable diagnostics / loopback | Funcții chip/board de confirmat, probe separate; fără a activa loopback în MEDIA1 | P | P | P | P |
| PTP / timestamping / AVB / PFC | Nu sunt revendicate; confirmare exactă înainte de orice proiect separat | P | P | P | P |

Ordinea propusă după acceptanța MEDIA1: inventar și contoare → filtrare/MTU/VLAN → checksum/TSO dacă hardware și API confirmă suportul → energie/EEE/ASPM → wake/offload → funcțiile avansate confirmate. Fiecare schimbare funcțională primește candidat separat, scope și matrice de regresie în cele patru celule. Capabilitățile absente primesc UNSUPPORTED; cele neconfirmate rămân UNKNOWN.

## Referințe de interpretare

- [Linux mii.h — rezoluția PAUSE, tabelul IEEE 28B-3](https://kernel.googlesource.com/pub/scm/linux/kernel/git/bpf/bpf-next/+/169e77764adc041b1dacba84ea90516a895d43b2/include/linux/mii.h).
- [Linux networking API — PHY pause negotiation](https://www.kernel.org/doc/html/v6.3/networking/kapi.html). Negocierea și programarea/efectul MAC sunt niveluri distincte.
- SDK-ul local fixat: IONetworkMedium.h/if_media.h expun numai kIOMediumOptionFlowControl/IFM_FLOW; IONetworkController.h cere activeMedium din dicționarul publicat și medium null la link inactiv.
- [Realtek RTL8111H(S), referință de familie](https://www.realtek.com/Product/Index?cate_id=786&id=3959&menu_id=1008). Nu substituie inventarul exact al celor două plăci.
