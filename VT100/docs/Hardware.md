# Carrier Board for Pi Zero VT100 Terminal


This project designs and builds a carrier board for the Raspberry Pi Zero that, together with the Pi Zero software, enables a bare‑metal implementation of a VT100 terminal.
The software features are described in the `VT100` directory.

The carrier board can be mounted directly in the rear panel of a terminal enclosure of a 60% VT100 replica designed by megardi (https://www.instructables.com/23-Scale-VT100-Terminal-Reproduction/) and provides the following components:
- 5 VDC / 2 A power supply (input 7–9 V DC or AC / 2 A wall adapter)
- RS‑232 interface to the host computer with MAX3232
- Alternative host interface via Mini‑DIN‑6 connector and 4‑pin header
- USB‑A connector for a wired USB keyboard
- Switch (relay) to swap RxD and TxD for RS-232
- 800 Hz buzzer (optional)

The Pi Zero GPIO pins are not 5 V tolerant. Therefore, all incoming signals (here: RxD only) must be level‑shifted from 5 V to 3.3 V. This applies to the Mini DIN6 connector to the MBC2-Z80. The RxD level shifting is implemented with a resistor divider: 820 Ω and 1.5 kΩ. No issues have been observed even at 115200 baud.

To provide the RS-232 connection I use a MAX3232 board with Vcc connected to the 3.3V output of the Pi zero which solves this compatibility problem, as all in and outgoing signals are 3.3V.

<div style="text-align: center;">
  <img src="Schematic_V22.png" alt="Schematic Adapterboard" width="60%" height="auto"/>
  <img src="Layout_V22.png" alt="Layout Adapterboard" width="60%" height="auto"/>
</div>


The images above show the schematic and the layout of the last version which uses a LM2576 switching regulator to provide the 5V 2A power line for the display controller and the MBC2 connected through DIN6 connector. If a LM2576-adj is used both smd resitors can be used to select a voltage slighthly above 5V (eg. 5.1 - 5.2V) as I discovered sometimes some flickering of the screen when background ist reversed (white), which may be related to power fluctuations. If you use a LM2576-5V just one resitor has to be bridged (0R).

The usb keyboard input is wired to 2 testpoints below the usb C connector of the Pi. Klick and Bell sounds are generated via 800 Hz PWM signals from GPIO and a small buzzer. Switching of the Tx/Rx lines of the RS232 connector are done by a relay which is triggered by a GPIO pin.


All changes have been reflected in the schematic and layout.

An adapter for the MBC2‑Z80 to connect the Z80 via the Mini‑DIN‑8 connector is described in the sub‑project “Z80‑SBC_Adapter”.

Board revisions:
- [x] Barrel jack for 9 V DC / AC input
- [x] Replace regulator LM2576 switching regulator (LM2576-50 / LM2576-adj)
- [x] LED indicator On/Off
- [x] Contacts for power switch
- [x] RxD <> TxD swap via relay, switchable by Pi GPIO pin
- [x] USB‑A socket for keyboard connection
- [x] Mini‑DIN‑6 socket for direct MBC2 connection with power
- [x] RS‑3232 DB9 connector
- [x] Internal header for direct MBC2 connection inside the terminal enclosure
- [x] PCB cut‑out for the USB plug (very short pins)
- [x] Buzzer / speaker circuit added
    - [x] Pads for piezo buzzer, 12.5 mm diameter, 5 mm pitch
- [x] Schematic revised for external fabrication

## Backplate for VT100 case to mount board 

A new backplate has been developed in OpenScad to be able to mount the pcb and insert it in the opening slot at the back of the 60% VT100 replica. 


<div style="text-align: center;">
  <img src="Backplate_Front.png" alt="Schematic Adapterboard" width="60%" height="auto"/>
  <img src="Backplate_Back.png" alt="Layout Adapterboard" width="60%" height="auto"/>
</div>

Holes allow to add on/off toggle switch, give access to power connector, RS-232, Mini DIN6 and USB connector. The slot above the usb connector could be used to mount a SD-card extension cable to accomodate switching of sd card at the back of the VT100 case.

To connect the display controller to power and the hdmi connector at the Pi additional custom cabeling has to be used depending on the connections at the display controlller.

## Revision status

- Carrier board schematic V2.2 was created with a switching power-supply variant (LM2576-50 / LM2576-adj).
- VT100 terminal integration was validated with a provisional backplate and carrier board V2.0 and also worked seemlessly with V2.2.
- A routing issue near the rectifier in V2.0 required a follow-up revision.
- Revision V2.1 and V2.2 use a 40 V / 2 A DIP rectifier and support AC/DC supplies up to 12 V. With this version any polarity of the power connector can be used. 
- OpenSCAD backplate files were updated to support carrier board V2.1 and above.
- Version 2.2 moved the resistor devider for RxD to the Mini DIN6 connector and rewired the MAX3232 to be powered by 3.3v for maximum compatibility with Pi.

## Firmware interaction notes (2026-02-18)

- Keyboard hotkeys currently used by firmware runtime: `F12` (legacy setup), `F11` (modern setup), `F10` (local keyboard loopback mode).
