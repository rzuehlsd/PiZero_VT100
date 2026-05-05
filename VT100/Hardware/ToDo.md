# Hardware To-dos

## GPIO Protection and Relay Signal Path (Next Board Revision - **MANDATORY**)

### RxD/TxD protection redesign
- [ ] Place 1 kΩ series resistor on RxD only (not TxD) — TxD series resistor drops 3.3V Pi output below host TTL threshold
- [ ] Move 1 kΩ resistor from GPIO side to **DIN connector side, before the relay**
  - **Why:** In null-modem cable scenario, host TxD (5V) reaches Pi TxD GPIO unprotected until relay switches
  - Moving resistor before relay ensures all incoming 5V signals are protected regardless of relay position
  - **Specification:** 1 kΩ resistors, 0603 SMD preferred
  - **Test:** Verify 3.3V TxD level at host input at 115200 baud; verify GPIO stays below 3.6V at 5V input

### Relay mismatch vulnerability (known risk in V2.2)
- [ ] Document in user guide: always switch relay before connecting null-modem cable
- [ ] Consider adding a software warning or power-on relay auto-detection

- [ ] Update schematic V2.3 with corrected resistor placement
- [ ] Update PCB layout V2.3 accordingly

## Flackerndes Display bei hellem Hintergrund

- Prüfen der Spannungen am Display-Controller
- Falls Spannungen fluktuieren:
  - den LM2576-5V durch einen LM2676-adj ersetzen und 
  - die Ausgangsspannung auf 5.2 V einstellen
- Achtung:
  - Abklären, welche maximale Eingangsspannung der Pi Zero verkraften kann!