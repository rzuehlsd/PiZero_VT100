# Trägerplatine für Pi Zero VT100 Terminal
<div align="center">
RZ 9/2015
</br>
</br>
</div>


In diesem Projekt wird eine Trägerplatine für den Raspberry Pi Zero designed und gebaut, die zusammen mit der Software pigfx_V20 eine Bare Metal Implementierung eines VT100 Terminals ermöglicht.
Die Softwareeigenschaften sind im Projekt /Volumes/SSD1000/2_Workbench/3_SW_Projekte/8_PI_Firmware/pigfx_v20 beschrieben, einschließlich der Erweiterungen gegenüber der Orginalversion. Die Software Version ist ein Erweiterung der Version pigfx von Filippo Bergamasco.

Die Trägerplatine kann direkt in der Rückwand eines Terminalgehäuses eingebaut werden und stellt die folgenden Komponenten bereit:
- 5VDC / 1 A Netzteil (Eingangspannung 7 - 9V DC oder AC / 1A Steckernetzteil)
- RS232 Schnittstelle zum Host Computer
- alternative Hostschnittstelle über Mini DIN8 Stecker sowie 4-polige Stiftleiste
- USB A Stecker für Anschluss eine USB Keyboards (kabelgebunden)

Die GPIO Pins des PI Zero sind nicht 5V tollerant, d.h. alle die eingehenden Signale (hier nur RxD) müssen von 5V Pegel auf 3,3V Pegel umgesetzt werden. Ausgehende Signale sind bezüglich der Pegel kompatibel mit 5V Systemen.
In einer ersten Version wurde hier ein 74GC4050 als Pegelwandler verwendet, was jedoch nicht verlässlich funktionierte.
Da die RS232 Schnittstelle mit einem MAX3232 realisiert ist und somit auch mit einer Versorgungsspannung von 3,3V zufrieden ist, wurde die Pegelumsetzung des RxD-Signals letzendlich über einen Spannungsteiler aus einem 820Ohm und einem 1,5KOhm Widerstand realisiert. Bisher sind auch bei einer Geschwindigkeit von 115200 Baud keine Probleme aufgetreten.

Der Aufbau der geänderten Version erfolgte testweise auf der alten Platinenvariante. Dazu wurde der 74HC5040 ausgelötet und das RxD Signal über den Spannungsteiler an den Pi gelegt. Alle anderen Signale wurden über entsprechende Drahtbrüclen direkt an den Pi Zero geführt.

Beim Test stellte sich auch heraus, dass der 7805 Spannungsregler für einen stabilen Betrieb mit einem kleinen Kühlkörper versehen werden muss. Auf der alten Platine wurde hier provisorisch für Testzwecke ein kleiner Kühlkörper verbaut. Der 7805 ist mit dem Kühlkörper stabil und hat neben dem Pi Zero auch den MSBC2-Z80 mit CPM moit Spannung versorgt.

Alle Änderungen wurden dann im Schaltplan und im Layout berücksichtigt.

Ein Adapter für den MBC2-Z80 zur Anbindung des Z80 über den Mini DIN8 Stecker wird im Teilprojekt Z80-SBC_Adapter beschrieben. Auf einen RS232 Anschluss wurde hier verzichtet. Neben dem Mini DIN8 Stecker kann der SBC aber auch über ein 4-poliges Jumper Kabel direkt mit der Adapterplatine verbunden werden.