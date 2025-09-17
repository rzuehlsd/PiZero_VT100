# Trägerplatine für Pi Zero VT100 Terminal
<div align="center">
RZ 9/2015
</br>
</br>
</div>


In diesem Projekt wird eine Trägerplatine für den Raspberry Pi Zero designed und gebaut, die zusammen mit der Software pigfx_V20 eine Bare Metal Implementierung eines VT100 Terminals ermöglicht.

Die Softwareeigenschaften sind im Projekt /Volumes/SSD1000/2_Workbench/3_SW_Projekte/8_PI_Firmware/pigfx_v20 beschrieben, einschließlich der Erweiterungen gegenüber der Orginalversion. Die Software Version ist ein Erweiterung der Version pigfx von Filippo Bergamasco.

Die Trägerplatine kann direkt in der Rückwand eines Terminalgehäuses eingebaut werden und stellt die folgenden Komponenten bereit:
- 5VDC / 2 A Netzteil (Eingangspannung 7 - 9V DC oder AC / 1A Steckernetzteil)
- RS232 Schnittstelle zum Host Computer
- alternative Hostschnittstelle über Mini DIN8 Stecker sowie 4-polige Stiftleiste
- USB A Stecker für Anschluss eine USB Keyboards (kabelgebunden)
- Umschalter für Tausch von RxD und TxD über Relais 
- 800Hz Buzzer (optional)

Die GPIO Pins des PI Zero sind nicht 5V tollerant, d.h. alle die eingehenden Signale (hier nur RxD) müssen von 5V Pegel auf 3,3V Pegel umgesetzt werden. Ausgehende Signale sind bezüglich der Pegel kompatibel mit 5V Systemen.

Die Pegelumsetzung des RxD-Signals wird über einen Spannungsteiler aus einem 820Ohm und einem 1,5KOhm Widerstand realisiert. Bisher sind auch bei einer Geschwindigkeit von 115200 Baud keine Probleme aufgetreten.

Der Aufbau der geänderten Version erfolgte testweise auf der alten Platinenvariante. Dazu wurde der 74HC5040 ausgelötet und das RxD Signal über den Spannungsteiler an den Pi gelegt. Alle anderen Signale wurden über entsprechende Drahtbrüclen direkt an den Pi Zero geführt.

Beim Test stellte sich auch heraus, dass der 7805 Spannungsregler für einen stabilen Betrieb mit einem kleinen Kühlkörper versehen werden muss. Auf der alten Platine wurde hier provisorisch für Testzwecke ein kleiner Kühlkörper verbaut. Der 7805 ist mit dem Kühlkörper stabil und hat neben dem Pi Zero auch den MSBC2-Z80 mit CPM moit Spannung versorgt.

In der Endversion muss jedoch auch das LCD Display über die 5V Spannung mit ca 1A versorgt werden. Damit muss die 5V Spannungsversorgung mindestens 2A liefern können und mit einem größeren Kühlkörper von ca. 6 K/W Wärmeleitwiderstand versehen werden. 

Alle Änderungen wurden im Schaltplan und im Layout berücksichtigt.

Ein Adapter für den MBC2-Z80 zur Anbindung des Z80 über den Mini DIN8 Stecker wird im Teilprojekt Z80-SBC_Adapter beschrieben. 

Die Platine wurde überarbeitet:
- [x] Hohlstecker für 9V DC / AC Spannung
- [x] Änderung für Spannungsregler LM350 einbauen 
- [ ] Kühlkörper prüfen und ggf. größeren einbauen
- [X] LED Anzeige On/Off
- [x] Kontakte für Ein/Ausschlater
- [x] Umschalter RxD <> TxD bzw Relais schaltbar durch Pi GPIO Pin
- [x] USB A Buchse für Keyboard-Anschluss
- [x] Mini DIN6 Buchse für direkten Anschluss des MBC2 mit Spannungsversorgung
- [x] RS232 DB9 Buchse
- [x] interne Steckerleiste für direkten Anschluss des MBC2 im Terminalgehäuse
- [x] Platinenaussparung für den USB Stecker einbauen (Anschlusspins sehr kurz)
- [x] Schaltung für Buzzer / Lautsprecher eingebaut
    - Ansteuerung über GPIO12 über PWM (muß mit Overlay auf PWM gesetzt werden) oder direkte Ansteuerung über pigpio
    - Beispielcode für PWM:
    ```
        int gpioHardwarePWM(12, 800, x% * 1000000) 
        delay(250m)
    ```
    - Kontakte für Piezzobuzzer mit 12,5mm Durchmesser und 5mm Rastermaß zusätzlich zu Stecker für Lautsprecher vrgesehen
- Schaltung überarbeitet für externe Fabrikation 