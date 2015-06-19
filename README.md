# esp8266-basic-bridge

This is the "basic" esp8266 WiFi to serial bridge, in other words, it accepts connections
on tcp port 23, gets all data from it, sends it to the UART (serial port) and the other way
around. This is the way to go to make your non-networking microcontroller WiFi-ready. If you
add an RS-232C buffer (something like a MAX232 or similar), you can even make your
non-networking peripherals like printers etc. available over the wireless lan.

This implementation is certainly not the first, there are various others implementations around
but I decided to make my own for various reasons:

- no pain no gain, in other words, just using someone else's stuff won't learn you anything;
- I noticed the existing implementations were heavily based on Espressif's examples, the
  examples aren't quite stunning when it comes to programming skill level, they're clumsy and
  not quite efficient;
- the exisiting implementations tend to be bloated, way too much code is copied/pasted which
  makes it hard to understand;
- none of the existing implementations does (did?) feature a completely transparent channel
  where data forwarding is carried out over one tcp port and setting up the bridge is done
  over another tcp port;
- not much active development on most of them
- my version implements heavy buffering and is completely interrupt-driven, so the chances on
  dropping bytes or buffers getting overrun is are very small, even at high uart speeds (I am
  currently using 460800 baud...);
- I'm using next to none of all of the borked sdk functions, I try to address the hardware
  directly wherever possible, if documentation is available (which, unfortunately, is quite
  sparse); hopefully creating good examples for those who are planning to start developing on
  the esp8266 but are reluctant to start due to the lack of documentation.

Having said this, it's very loosely based on the work of beckdac
(https://github.com/beckdac/ESP8266-transparent-bridge), which I used as a starting point,
although there hasn't been any original code left for some time ;-) 

This is how it works:

- attach your microcontroller's uart lines or RS232C's line driver lines to the ESP8266, I think
  enough has been written about how to do that;
- load a generic "AT-command" style firmware into the ESP8266; exactly how to do that should be
  on the page you're downloading the firmware from;
- setup the wlan parameters (SSID and password) from this firmware, using a terminal emulator;
  check it's working and write to flash (if the firmware doesn't do that automatically);
- now flash this firmware, for example with the esptool.py command, something like this:
  esptool.py --port /dev/pl2303 --baud 460800 write_flash 0x00000 fw-0x00000.bin 0x40000 fw-0x40000.bin,
  replacing /dev/pl2303 by the proper device node; the esp8266 can indeed be flashed at this high
  speed, it's autoprobing the baud rate; if it doesn't succeed immediately, try again, sometimes
  the esp8266 gets the baud rate wrong; if it still doesn't work, try a lower baud rate;
- after flashing restart; in theory this is not necessary, but I found the uart won't start if you
  leave out the reset;
- start a telnet to port 24 of the ip address, type help and enter;
- you will now see all commands;
- use the commands starting with baud to setup the uart, after that, issue the config-write
  command to save and use the reset command to restart;
- after restart you will have a transparent connection between port 23 and the uart and port 24
  remains available for control.

These are the currently implemented commands on port 24:

	short	long			parameters		description

		cd	config-dump		none			shows the complete configuration as
											stored in non volatile memory
		cw	config-write	none			writes config to non volatile memory
		?	help			none			shows list of commands
		pd	print-debug		0 or 1			0 = enable, 1 = enable
											toggle printing of debug info during
											startup (dhcp, wlan assocation)
		q	quit			none			disconnect the control channel
		r	reset			none			reset the esp8266, necessary if you
											make changes to the uart parameters
		s	stats			none			shows some statistics, these may 
											change over time
		st	strip-telnet	0 or 1			0 = disable, 1 = enable
											toggle stripping of telnet "garbage"
											at the start of a connection; don't
											forget to write using cw!
		ub	uart-baud		baud rate		any baud rate you like, forget the
											300-600-1200-2400-etc. list, the
											esp8266 can freely use any baud rate
											you like; don't forget to write and
											reset
		ud	uart-data		5 to 8			the number of data bits, usually 8
		us	uart-stop		1 or 2			the number of stop bits, usually 1
		up	uart-parity		none/even/odd	the parity, usually none; don't
											forget to set data bits to 7 if you
											want to use parity
		wd wlan-dump		none			show everything known about the curent
											wlan connection, like ssid, channel and
											signal strength.

More commands will follow, especially for driving the gpio2 pin, probably also pwm and maybe even
i2c.

See here for latest news and join the discussion: http://www.esp8266.com/viewtopic.php?f=11&t=3212.
