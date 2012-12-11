
Jeeheater
===========

A Jeenode (jeelabs.com) Arduino board sketch for monitoring and log data from multiple Dallas DS18B20 temperature sensors.
Sends data to EmonCMS (emoncms.org) server for reporting temperature measurements.

===========
Can be used as part of the: openenergymonitor.org project
Licenced under GNU GPL V3
http://openenergymonitor.org/emon/license

EtherCard Library by Jean-Claude Wippler and Andrew Lindsay
JeeLib Library by Jean-Claude Wippler

CHANGELOG
===========
2012-08-06

Version 1.04
- First release based on the Roomboard example sketch of the Jeelib library from Jean-Claude Wippler
- Receive data from the attached Roomboard plug with custom made Dallas DS18B20 temperature sensors on it
- Adjustable measurement timings and average calculations to make smooth temperature readings
- Extra Low power features (disable ADAC) to extend battery life.
- Relay's data to EmonCMS server with Radio communication and JSON strings.
- Ping back / receive check from the data.




