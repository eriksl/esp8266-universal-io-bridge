# esp8266-basic-bridge
A very basic esp8266 WiFi to serial bridge, little features but also very simple code.
It's very suitable as starting point for implementing new features using a proper uart
implementation and an extra tcp command channel with complete command and parameter
parsing already included.

It's very loosely based on the work of beckdac (https://github.com/beckdac/ESP8266-transparent-bridge),
but there is no original code left.

Please note this implementation has a very efficient, completely interrupt-driven uart handler.

See here for latest news and join the discussion: http://www.esp8266.com/viewtopic.php?f=11&t=3212.
