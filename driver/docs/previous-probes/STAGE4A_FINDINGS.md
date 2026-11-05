# Baseline stabil înainte de power management

stage3g-stability-20261005-032746.tgz: driver 0.16.0, MSI index1, zece runde de 256 MiB în fiecare sens, total 5 GiB. Toate 20 de transferuri au SHA256 PASS. 57,128 secunde; upload min/median/max 491,57/799,125/849,52 Mbit/s; download 860,17/926,995/935,30 Mbit/s. 2.051.577 TX submitted și completed, 1.990.936 RX delivered, fără erori sau drop-uri. Zero filtre IRQ unqualified. 238.890 ring-full retries sunt backpressure, nu drop-uri. Pool și ring TX libere la final, fără watchdog/unsafe retention. SHA256 nu dovedește singur lipsa retransmisiilor.

Rămâne problema raportată: Ethernet nu revine automat după sleep, manual disable/enable îl repornește. 0.16.0 nu implementează registerWithPolicyMaker/setPowerState. Sursa Apple IONetworkController::prepare arată că hook-ul implicit unsupported determină PMstop. 0.17.0 implementează hook-ul corect și callback-ul de power serializat prin command gate. IOEthernetInterface notifică disable/enable în jurul pierderii/restaurării power; controlul administrativ rămâne al framework-ului.

Nu pretindem că baseline-ul de transfer dovedește sleep/wake sau că noul callback a reparat deja hardware-ul. Următorul test verifică minimum o trecere sleep/wake, fără reactivare manuală înainte de colectare. În caz de eșec, dovezile păstrează starea PM și PCI/MMIO/mapper pentru diagnostic.
