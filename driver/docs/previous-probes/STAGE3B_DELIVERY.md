# STAGE3B — primul test de rețea macOS

## Ce testezi acum

Interfață Ethernet reală, DHCP și ping cu AppleVTD activ. Nu mai este necesar emițătorul UDP de pe celălalt calculator: driverul primește și trimite pachetele sistemului fără o limită finită.

Această versiune este compilată și testată local. Reușita STAGE3A nu demonstrează încă funcționarea acestei integrări; rezultatul de hardware STAGE3B va veni din următoarea captură de pe Acer.

## 1. Kext-ul și compilarea

Kext-ul inclus este:

`build/Release/RealtekVTDNextEthernet.kext`

Dacă dorești să compilezi pe Acer, din directorul acestei versiuni:

```bash
bash TEST_LOCAL.command
bash BUILD_DRIVER.command --fetch-sdk
```

Firmware-ul din `firmware/rtl_nic/` este inclus automat. SDK-ul este fixat la commit-ul documentat; dacă este deja disponibil poți folosi `MAC_KERNEL_SDK=/cale/MacKernelSDK bash BUILD_DRIVER.command`. Build-ul nu încarcă driverul și nu modifică sistemul sau EFI.

## 2. Boot și DHCP

Folosește noul **RealtekVTDNextEthernet.kext** în configurația de boot pe care o gestionezi tu. Pentru același dispozitiv nu încărca simultan vechiul RealtekVTDNextProbe sau RealtekRTL8111.

1. Conectează cablul Ethernet la rețeaua cu server DHCP/router.
2. Bootează cu AppleVTD activ.
3. În setările de rețea macOS identifică interfața nouă și selectează configurarea IPv4 prin DHCP, dacă sistemul nu a ales-o deja. Numele BSD poate fi en0, en1 sau alt enX; nu îl presupunem.
4. Așteaptă 60 de secunde după login. Pentru acest prim test păstrează calculatorul treaz.

## 3. Test și colectare

Din directorul acestei versiuni:

```bash
bash TEST_NETWORK.command
bash COLLECT_LOCAL.command
```

Prima comandă identifică interfața **care aparține acestui controller**, verifică adresa și lease-ul DHCP, apoi trimite 10 ping-uri către router, legate de interfața respectivă și de adresa ei IPv4. Verifică și creșterea contoarelor TX/RX ale driverului. Nu schimbă setările de rețea. Nu este suficient ca ping-ul să meargă prin Wi-Fi.

Dacă routerul nu răspunde la ICMP, folosește adresa IPv4 a celuilalt calculator din aceeași rețea cablată, după ce Acer a primit DHCP:

```bash
bash TEST_NETWORK.command --target 192.168.1.10
bash COLLECT_LOCAL.command
```

Înlocuiește adresa exemplului cu adresa reală. Dacă testul afișează NOT PASS, rulează oricum colectarea; nu este nevoie să repeți testele vechi cu emițătorul.

**Trimite arhiva afișată de colectare:**

`build/local-evidence/stage3b-ethernet-AAAALLZZ-HHMMSS.tgz`

Conține interfața, proprietățile driverului, AppleVTD/mapările, logul kernel și ultimul rezultat DHCP/ping. Colectarea este doar citire. Captura poate conține adrese și nume din rețeaua locală.

## Criteriul de trecere

- `Stage3BEnabled = Yes`, `Stage3BStatus = 0`, mapper al dispozitivului prezent și AppleVTD activ în captură.
- Interfața controllerului are un lease DHCP cu aceeași adresă IPv4 ca interfața.
- 10/10 răspunsuri ping prin acea interfață și progres atât TX, cât și RX în contoarele driverului.
- `Stage3BUnsafeMappingRetained = No`, fără watchdog sau erori de traducere DMA relevante.

`ChipCommand = 12`, BME activ și contoarele care continuă să crească sunt normale cât timp interfața este activă; proba nu se mai oprește după 32/64 pachete.

## Arhitectura acestei etape

Inițializarea PHY/MAC/firmware validată anterior este reutilizată din implementarea nouă. Calea finită STAGE3A este înlocuită cu `NetworkProgram`: producător/consumator TX și consumator RX continuu, fiecare cu ring de 64 de descriptori și marcaj EOR la capăt.

TX mapează direct lanțul mbuf în contextul consumatorului cozii, în afara workloop gate-ului; nu există buffer TX permanent de copiere. Sunt acceptate maximum patru segmente DMA per pachet. Fragmentele secundare sunt publicate înainte de OWN al primului fragment; mbuf-ul este eliberat numai când ultimul descriptor este completat. În cazul ringului plin, coada păstrează pachetul și este repornită asincron după IRQ.

RX copiază pachetele valide în mbuf-uri, elimină FCS, livrează către IONetworkInterface și rearmează descriptorii în ordinea ringului. Cadrele invalide sau lipsa unui mbuf se contabilizează ca drop, fără oprirea întregii sesiuni. Checksum/VLAN stripping în hardware sunt dezactivate; sistemul face checksum-ul în software.

Disable oprește întreruperile/motoarele și verifică dezactivarea BME înainte de eliberare. Dacă dezactivarea nu poate fi confirmată, resursele și proprietarul rămân reținute. Enable reexecută inițializarea hardware înainte de publicarea ringurilor, pentru a evita cursorii moșteniți. Acest lifecycle este verificat în simulare, nu încă pe Acer.

Timerul nu examinează OWN pentru completări. Dacă există TX pending fără completări timp de zece secunde, oprește interfața și publică `Stage3BTXWatchdog`; recuperarea automată și sleep/wake nu sunt implementate aici.

Referințe primare: regulile descriptorilor/FCS din [Linux r8169](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/realtek/r8169_main.c); contractele macOS din headerele SDK-ului fixat, inclusiv restricția `IODMACommand::prepare` în afara unui gate și [IOBasicOutputQueue](https://developer.apple.com/documentation/kernel/iobasicoutputqueue).

## Următorul pas după acest test

Dacă DHCP/ping trec, testăm disable/enable pe hardware și revenirea traficului după deconectarea/reconectarea cablului. Apoi urmează sleep/wake, recuperare și stres. Nu declarăm încă driverul validat pentru utilizare zilnică.
