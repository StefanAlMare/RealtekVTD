# STAGE4A POWER 0.17.0 — prima probă sleep/wake

Extrage ZIP-ul în /Volumes/USB/A pe Acer. Kext compilat cu firmware: build/Release/RealtekVTDNextEthernet.kext. Alex gestionează boot/EFI cu AppleVTD activ. Instrumentele nu instalează și nu încarcă kext-ul.

Recompilare opțională:

```bash
cd /Volumes/USB/A/RealtekVTDNext-0.17.0-STAGE4A-POWER
bash TEST_LOCAL.command
bash BUILD_DRIVER.command --fetch-sdk
```

După boot, Ethernet activ, întâi pe Acer:

```bash
cd /Volumes/USB/A/RealtekVTDNext-0.17.0-STAGE4A-POWER
bash BEFORE_SLEEP.command
```

Continuă numai după test DHCP/ping și power baseline PASS. Dacă nu trece, trimite arhiva produsă sau rulează COLLECT_LOCAL.command; nu începe proba sleep.

Pune laptopul în sleep manual, din meniul Apple sau prin clapetă. Lasă-l cel puțin 60 secunde. Trezește-l și așteaptă 30 secunde pentru link/DHCP. Păstrează cablul conectat. Fără disable/enable manual, pe Acer:

```bash
cd /Volumes/USB/A/RealtekVTDNext-0.17.0-STAGE4A-POWER
bash AFTER_WAKE.command
```

Trimite arhiva stage4a-power-*.tgz afișată, inclusiv dacă testul eșuează. AFTER_WAKE verifică DHCP/ping, PM sleep/wake counters și failure flags, apoi colectează dovezile oricum. Abia după colectare poți folosi disable/enable pentru conversație dacă Ethernet este nefuncțional. Nu trebuie pornit iMac-ul pentru prima probă.

Criteriu: minimum o creștere SleepCount și WakeCount față de baseline, PowerRegistered=true, Suspended=false, interfață activă fără reactivare manuală, 10/10 ping replies și fără erori PM noi. Testul de trafic LAN după wake și repetarea mai multor cicluri vor urma numai după această probă; o revenire de 60 secunde nu validează somnul peste noapte.
