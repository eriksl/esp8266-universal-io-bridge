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

<table>
<tr>
<th>short</th><th>long</th><th>parameters</th><th>description</th>
</tr>
<tr>
<td>st</td><td>strip-telnet</td><td><b>0</b> or <b>1</b></td><td><b>0</b> = disable, <b>1</b> = enable, toggle stripping of telnet garbage at the start of a connection when using the <b>telnet</b> command.
</tr>
<tr>
<td>ub</td><td>uart-baud</td><td><i>baud rate</i></td><td>Any <i>baud rate</i> you like, forget the 300-600-1200-2400-etc. list, the ESP8266 can freely use any baud rate you like. Don't forget to <b>config-write</b> and <b>reset</b>.</td>
</tr>
<tr>
<td>ud</td><td>uart-data</td><td><b>5</b>, <b>6</b>, <b>7</b>, or <b>8</b></td><td>The number of data bits, usually 8.</td>
</tr>
<tr>
<td>us</td><td>uart-stop</td><td><b>1</b> or <b>2</b></td><td>The number of stop bits, usually 1.</td>
</tr>
<tr>
<td>up</td><td>uart-parity</td><td><b>none</b>, <b>even</b> or <b>odd</b></td><td>The parity, usually <b>none</b>. Don't forget to set data bits to <b>7</b> if you want to use any parity.</td>
</tr>
</table>

### Commands concerning configuration:
<table>
<tr>
<th>short</th><th>long</th><th>parameters</th><th>description</th>
</tr>
<tr>
<td>cd</td><td>config-dump</td><td><i>none</i></td><td>Shows the complete configuration as stored in non volatile memory.</td>
</tr>
<tr>
<td>cw</td><td>config-write</td><td><i>none</i></td><td>Writes config to non volatile memory.</td>
</tr>
</table>

### Commands concerning GPIO:

<table>
<tr>
<th>short</th><th>long</th><th>parameters</th><th>description</th>
</tr>
<tr>
<td>gd</td><td>gpio-dump</td><td><i>none</i></td><td>Dump GPIO configuration and current status.</td>
</tr>
<tr>
<td>gg</td><td>gpio-get</td><td><i>gpio</i></td><td>Read GPIO. For example <b>gg 2</b> reads the GPIO <b>2<b> as input.</td>
</tr>
<tr>
<td>gs</td><td>gpio-set</td><td><i>gpio</i> [<i>value</i>]</td><td>Set the GPIO if it's set as output, if set as bounce, trigger a bounce, if it's set as pwm, set the duty cycle (default startup value is taken if it's missing).</td>
</tr>
<tr>
<td>gm</td><td>gpio-mode</td><td><i>mode</i> [<i>mode parameters</i>]</td><td>Without mode/parameters: dump all GPIOs and their mode. See the table below for available modes and their syntax when parameters are supplied. After making changes, <b>reset</b> to enable the changes.</td>
</tr>
</table>
<table>
<tr>
<td><b>gm</b> <i>gpio</i> <b><i>mode</i></b></td><td><b>parameters</b></td><td>description</td>
</tr>
<tr>
<td>disable</td><td><i>none</i></td><td>Disable the GPIO (leave completely untouched).</td>
</tr>
<tr>
<td>input</td><td><i>none</i></td><td>Set GPIO to input.</td>
</tr>
<tr>
<td>output</td><td><i>startup-state</i></td><td>Set GPIO to output, set <i>startup-state</i> to <b>0</b> or <b>1</b> to configure the state of the output directly after boot.</td>
</tr>
<tr>
<td>bounce</td><td><i>direction</i> <i>delay</i> <i>repeat</i> <i>autotrigger</i></td><td>
<table>
<tr><td><i>direction</i> is either <b>up</b> or <b>down</b>, specifying whether to bounce from "off" or "on".</td></tr>
<tr><td><i>delay</i> is a value in milliseconds between triggered state and resuming normal GPIO state.</td></tr>
<tr><td><i>repeat</i> is <b>0</b> or <b>1</b>. <b>0</b> means run once, <b>1</b> means repeating until stopped manually.</td></tr>
<tr><td><i>autotrigger</i> is <b>0</b> or <b>1</b>. <b>0</b> means leave the GPIO alone after start, need to trigger manually (using gpio-set), <b>1</b> means trigger automatically after start.</td></tr>
</table>
</td>
</tr>
<tr>
<td>pwm</td><td>[<i>startup-duty-cycle</i>]</td><td><i>startup duty cycle</i> is the duty cycle after boot, default <b>0</b>. The duty cycle can be 0 (off) to 65535 (continuously on).</td>
</tr>
<tr>
<td>i2c</td><td><b>sda</b> or <b>scl</b></td><td>configure this GPIO for i2c operation. Specify two GPIOs to assign to <b>sda</b> and <b>scl</b>. </td>
</tr>
</table>
### I2C related commands
<table>
<tr>
<th>short</th><th>long</th><th>parameters</th><th>description</th>
</tr>
<tr>
<td>ia</td><td>i2c-address</td><td><i>address</i></td><td>set i2c slave's client address for subsequent read and write commands.</td>
</tr>
<tr>
<td>ir</td><td>i2c-read</td><td><i>bytes</i></td><td>Read (raw) this amount of bytes from the i2c slave. The bytes returned are in hex.</td>
</tr>
<tr>
<td>iw</td><td>i2c_write</td><td><i>byte</i> [<i>byte ...</i>]</td><td>Write (raw) bytes to i2c slave. Format is hex bytes separated by spaces, e.g. <b>01 02 03</b>.</td>
</tr>
<tr>
<td>irst</td><td>i2c-reset</td><td><i>none</i></td><td>Reset the i2c bus. Required whenever a read or write failed.</td>
</tr>
<tr>
<td>isd</td><td>i2c-sensor-dump</td><td>[<b>1</b>]</td><td>Dump all detected (known) sensors on the i2c bus. Add <b>1</b> to list all known sensors, including those that are not detected.</td>
</tr>
<tr>
<td>isr</td><td>i2c-sensor-read</td><td><i>sensor id</i></td><td>Get the value of the sensor with id <i>sensor id</i>. Obtain sensor id's with i2c-sensor-dump.</td>
</tr>
</table>
### Other commands:
<table>
<tr>
<th>short</th><th>long</th><th>parameters</th><th>description</th>
</tr>
<tr>
<td>?</td><td>help</td><td><i>none</i></td><td>Shows list of commands.</td>
</tr>
<tr>
<td>pd</td><td>print-debug</td><td><b>0</b> or <b>1</b></td><td><b>0</b> = enable, <b>1</b> = enable. Toggle printing of debug info during startup (dhcp, wlan assocation).</td>
</tr>
<tr>
<td>q</td><td>quit</td><td><i>none</i></td><td>Disconnect from the control channel.</td>
</tr>
<tr>
<td>r</td><td>reset</td><td><i>none</i></td><td>Reset the ESP8266, required if you make changes to the UART parameters or to the GPIOs mode.</td>
</tr>
<tr>
<td>s</td><td>stats</td><td><i>none</i></td><td>Shows some statistics, these may change over time.</td>
</tr>
<tr>
<td>wd</td><td>wlan-dump</td><td><i>none</i></td><td>Show everything known about the current wlan connection, like SSID, channel number and signal strength.</td>
</tr>
</table>

See here for latest news and join the discussion: http://www.esp8266.com/viewtopic.php?t=3959, old topic: http://www.esp8266.com/viewtopic.php?&t=3212.
