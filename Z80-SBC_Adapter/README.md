# Adapterplatine für MBC2-Z80 Board
<div align="center">
RZ 9/2015
</br>
</br>
</div>

Das MBC2 Board benötigt eine Adapterplatine für den Anschluss an das VT100 Terminal Board.
Das Board ist auf einer Trägerplatine aufgebaut, die mit 2 Distanzstücken oberhalb der MBC2 Platine montiert wird. Die elektrischen Verbindungen werden über die auf dem MBC2 vorhandene Steckerbuchse für den Seriell to USB Adapter hergestellt.

Auf der Adapterplatine sind die folgenden Funktionen realisiert:

- 5VDC Netzteil zur Spannungsversorgung des MBC2 über ein Steckernetzteil (7 - 9 VAC oder DC Spannung)
- Mini DIN6 Buchse für die Verbindung mit dem Terminal Board. Über diesen Stecker kann auch der MBC2 mit einer 5VDC Betriebsspannung versorgt werden.
- 4 poliger Pin Header zum direkten Anschluss des MBC2 über Jumper Kabel mit der Terminal Platine
- USB to Seriall Adapter für direkten Anschluss an Mac
- LED zu Anzeige der Betriebsspannung
- Ein/Aus Schalter


To Dos:

- [ ] Die auf dem MBC2 Board verbauten horizontalen Microschalter müssen durch vertikale Microschalter ersetzt werden, um auch außerhalb eines Gehäuses bedienbar zus ein
- [x] Konstruktion eines kleinen Gehäuses für den 3D Druck
- [x] Das Layout wurde komplett auf die Rückseite verlegt
- [ ] Verzögertes Einschalten implementieren, da Probleme mit VT100 falls zur gleichen Zeit Spannung anliegt -> prüfen!