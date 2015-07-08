# ESP8266 universal I/O bridge

## General

This is a project that attempts to make all (or at least most) of the I/O on the ESP8266 available over the network. Currently this includes GPIO's (as GPIO or PWM), I2C and the UART.

The GPIOs can be selected to work as "normal" input, "normal" output, "bounce" mode (this means trigger once, either manually or at startup, or "blink" continuously) or "pwm" mode (16 bits PWM mode, running at 330 Hz, suitable for driving lighting).

The "normal" UART lines (TXD/RXD) are available and are bridged to the ESP8266's ip address at port 25. This is unless the UART lines are re-assigned as GPIOs, which is also possible.

GPIOs can also be selected to work as i2c lines (sda, scl). You can use the "raw" i2c send and receive commands to send/receive arbitrary commands/data to arbitrary slaves. For a number of i2c sensors there is built-in support, which allows you to read them out directly, where a temperature etc. is given as result.

The firmware also listens at tcp port 24, and that is where the configuration and commands go to. Type `telnet <ip_address> 24` and type `?` for help.

The project is still in development, expect more features in the future.

## About the UART bridge

The UART bridge accepts connections on tcp port 23, gets all data from it, sends it to the UART (serial port) and the other way around. This is the way to go to make your non-networking microcontroller _WiFi_-ready. If you add an RS-232C buffer (something like a _MAX232_ or similar), you can even make your non-networking peripherals like printers etc. available over the wireless lan.

The UART driver is heavily optimised and is completely interrupt driven, no polling, which makes it very efficient.

This code is very loosely based on the work of beckdac (https://github.com/beckdac/ESP8266-transparent-bridge), which I used as a starting point, although there hasn't been any original code left for some time ;-) 

This is how it works:

- Attach your microcontroller's UART lines or RS232C's line driver lines to the ESP8266, I think enough has been written about how to do that.
- Load a generic "_AT-command_" style firmware into the ESP8266. Exactly how to do that should be on the page you're downloading the firmware from.
- Setup the _wlan_ parameters (___SSID___ and ___password___) from this firmware, using a terminal emulator. Check it's working and write to flash (if the firmware doesn't do that automatically).
- Now flash this firmware, for example with the esptool.py command, something like this: `esptool.py --port /dev/pl2303 --baud 460800 write_flash 0x00000 fw-0x00000.bin 0x40000 fw-0x40000.bin`. Replace _/dev/pl2303_ by the proper device node. The ESP8266 can indeed be flashed at this high speed, it's autoprobing the baud rate. If it doesn't succeed immediately, try again, sometimes the ESP8266 gets the baud rate wrong initially. If it still doesn't work, try a lower baud rate.
- After flashing restart. In theory this is not necessary, but I found the UART won't start if you leave out the reset.
- Start a `telnet` session to port 24 of the ip address, type `help` and `<enter>`.
- You will now see all commands.
- Use the commands starting with baud to setup the UART. After that, issue the `config-write` command to save and use the `reset` command to restart.
- After restart you will have a transparent connection between tcp port 23 and the UART, tcp port 24 is always available for control.

## About i2c

To work with i2c, first select two GPIO's as _sda_ and _scl_ lines using the `gm .. mode sda` and `gm .. mode scl` commands. Besides the "normal" GPIOs, it should be possible to use _GPIO0_ and _GPIO2_ for i2c (boot selection), just as _GPIO1_ and _GPIO3_ (normally connected to the UART) using a proper pull-up resistor, but I didn't try it myself.

The selected GPIO's are set to _open drain_ mode without _pull-up_, so you will have to add them yourself. The proper value of the resistors should be calculated but _4.7 kOhm_ usually works just fine. 

Use the `i2c-address`, `i2c-read` and `i2c-write` commands to read/write raw data to/from i2c slaves.

For a selection of well-known i2c sensors, there is built-in support. Use the `i2c-sensor-dump` and `i2c-sensor-read` commands for these.

Currently supported i2c sensors are:

- _digipicco_ (temperature and humidity)
- _lm75_ (and compatible sensors) (temperature)
- _ds1621/ds1631/ds1731_ (temperature)
- _bmp085_ (temperature and pressure)
- _htu21_ (temperature and humdity)
- _am2321_ (temperature and humidity)
- _tsl2560_ (light intensity)
- _bh1750_ (light intensity).

### Commands concerning the UART bridge:

short | long | parameters | description
-|-|-
st|strip-telnet|`0` or `1`|`0` = disable, `1` = enable, toggle stripping of telnet garbage at the start of a connection when using the `telnet` command.
ub|uart-baud|___baud rate___|Any ___baud rate___ you like, forget the 300-600-1200-2400-etc. list, the ESP8266 can freely use any baud rate you like. Don't forget to `config-write` and `reset`.
ud|uart-data|`5`, `6`, `7`, or `8`	|The number of data bits, usually 8.
us|uart-stop|`1` or `2`|The number of stop bits, usually 1.
up|uart-parity|`none`/`even`/`odd`|The parity, usually none. Don't forget to set data bits to `7` if you want to use any parity.

### Commands concerning configuration:

short | long | parameters | description
-|-|-
cd|config-dump|___none___|Shows the complete configuration as stored in non volatile memory.
cw|config-write|___none___|Writes config to non volatile memory.

### Commands concerning GPIO:

short | long | parameters | description
-|-|-
gd|gpio-dump|___none___|Dump GPIO configuration and current status.
gg|gpio-get|___gpio___|Read GPIO. For example `gg 2` reads the GPIO `2` as input.
gs|gpio-set|___gpio___ [___value___]|Set the GPIO if it's set as output, if set as bounce, trigger a bounce, if it's set as pwm, set the duty cycle (default startup value is taken if it's missing).
gm|gpio-mode|___mode___|Without parameters: dump all GPIOs and their mode. See the table below for available modes and their syntax when parameters are supplied. After making changes, `reset` to enable the changes.

mode | parameters | description
-|-|-
disable|___none___|Disable the GPIO (leave completely untouched).
input|___none___|Set GPIO to input.
output|___startup-state___|Set GPIO to output, set ___startup-state___ to `0` or `1` to configure the state of the output directly after boot.
bounce|___direction___ ___delay___ ___repeat___ ___autotrigger___|___direction___ is `up` or `down`, specifying whether to bounce from "off" or "on", ___delay___ is value in milliseconds between triggered state and resuming normal GPIO state. ___repeat___ is `0` or `1`. `0` means once, `1` means repeating until stopped manually. ___autotrigger___ is `0` or `1`, `0` means leave the GPIO alone after boot, need to trigger manually (using gpio-set), `1` means trigger automatically after boot.
pwm|[___startup-duty-cycle___]|___startup duty cycle___ is the duty cycle after boot, default `0`. The duty cycle can be 0 (off) to 65535 (continuously on).
i2c|`sda` or `scl`|configure this GPIO for i2c operation. Specify a GPIO for both `sda` and `scl`. The GPIO's are set to _open drain_ mode without _pull-up resistors_, so you will have to connect them yourself. The proper value of the resistors should be calculated but 4.7 kOhm usually works just fine. It should be possible to use GPIO0 and GPIO2 for i2c (boot selection), just as GPIO1 and GPIO3 (normally connected to the UART) with a proper pull-up resistor, but I didn't try it myself.
### I2C related commands
short | long | parameters | description
-|-|-
ia|i2c-address|___address___|set i2c slave's client address for subsequent read and write commands.
ir|i2c-read|___bytes___|Read (raw) this amount of bytes from the i2c slave. The bytes returned are in hex.
iw|i2c_write|___byte___ [___byte ...___]|Write (raw) bytes to i2c slave. Format is hex bytes separated by spaces, e.g. `01 02 03`.
irst|i2c-reset|___none___|Reset the i2c bus. Required whenever a read or write failed.
isd|i2c-sensor-dump|[`1`]|Dump all detected (known) sensors on the i2c bus. Add `1` to list all known sensors, including those that are not detected.
isr|i2c-sensor-read|___sensor id___|Get the value of the sensor with id ___sensor id___. Obtain sensor id's with i2c-sensor-dump.
### Other commands:
short | long | parameters | description
-|-|-
?|help|___none___|Shows list of commands.
pd|print-debug|`0` or `1`|`0` = enable, `1` = enable. Toggle printing of debug info during startup (dhcp, wlan assocation).
q|quit|___none___|Disconnect from the control channel.
r|reset|___none___|Reset the ESP8266, required if you make changes to the UART parameters or to the GPIOs mode.
s|stats|___none___|Shows some statistics, these may change over time.
wd|wlan-dump|___none___|Show everything known about the current wlan connection, like SSID, channel number and signal strength.

See here for latest news and join the discussion: http://www.esp8266.com/viewtopic.php?f=11&t=3212.
