# Hardware To-dos

## GPIO Protection and Relay Signal Path (Next Board Revision - **MANDATORY**)

### RxD/TxD protection redesign
- [x] 3,3V / %V Level Shifter mit BSS138 direkt vor den TX/RX Pins des PI zero einbauen


### Relay mismatch vulnerability (known risk in V2.2)
- [x] entfällt durch die neuen Level Shifter direkt vor dem Pi zero

## Flackerndes Display bei hellem Hintergrund
- [x] Prüfen der Spannungen am Display-Controller ok liegt bei knapp 5V
- Falls Spannungen fluktuieren:
  - den LM2576-5V durch einen LM2676-adj ersetzen und 
  - die Ausgangsspannung auf 5.2 V einstellen


## VT100 Adapter Board
- [x] ein am internen oder externen DIN6 Konnektor angeschlossener SBC kann nun über GPIO7 geschaltet werden
- [x] Code anpassen: GPIO7 als Schaltausgang für einen über den DIN6-Stecker angeschlossenen SBC ansteuern.
  - [x] Bei Start on/off als Parameter im Setup Dialog
  - [ ] Über Funktionstaste ein/ausschalten
  - [ ] Hardwaretest mit aktualisiertem Adapterboard und über DIN6 angeschlossenem SBC durchführen (steht noch aus)
