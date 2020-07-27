# azure-sphere-iot-bubble-machine
An Internet Connected Bubble Machine.  How awesome is that? This project was started for Hackathon 2020 (Microsoft's
Employee Hackathon). 

## Bubble Machine Parts

* 3D Printable Parts : the current STL's are in the STL directory.  You can also check
  [Thingiverse](https://www.thingiverse.com/thing:4551737) but these might be slighly behind. 
* DC Motor Controller : [TB56612FNG](
  https://www.ebay.com/itm/Dual-Motor-Driver-TB6612FNG-TB6612-DC-Stepper-Motor-Controller-Module-Boad-G25/164154483962?ssPageName=STRK%3AMEBIDX%3AIT&_trksid=p2057872.m2749.l2649)
* Blower Fan: [5v 5015 Blower](https://www.ebay.com/itm/2-Pcs-5V-Gdstime-DC-5015-mm-Blower-Radial-Cooling-Fan-Hotend-Extruder-RepRap/162858598407?ssPageName=STRK%3AMEBIDX%3AIT&_trksid=p2057872.m2749.l2649 )
* Wand Motor: [TT Motor](https://www.adafruit.com/product/3777)

## Nota Bene
This project uses C++ but Microsoft DOES NOT support C++ as a development language for Azure Sphere.   Azure Sphere is
an example of embedded development and is fairly "bare metal".  Even though we are using C++
the C++ library (i.e. STL) and much of the CRT (i.e. fopen) are not present in the tool-chain.  Beware!

