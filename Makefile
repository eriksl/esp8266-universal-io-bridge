SDKROOT			= /nfs/src/esp-open-sdk
SDKLD			= $(SDKROOT)/sdk/ld

CFLAGS			= -O3 -Wall -Wno-pointer-sign -nostdlib -mlongcalls -mtext-section-literals  -D__ets__ -DICACHE_FLASH
CINC			= -I$(SDKROOT)/lx106-hal/include -I$(SDKROOT)/xtensa-lx106-elf/xtensa-lx106-elf/include \
					-I$(SDKROOT)/xtensa-lx106-elf/xtensa-lx106-elf/sysroot/usr/include -I$(SDKROOT)/sdk/include -I.
LDFLAGS			= -nostdlib -Wl,--no-check-sections -u call_user_start -Wl,-static
LDSCRIPT		= -T$(SDKLD)/eagle.app.v6.ld
LDSDK			= -L$(SDKROOT)/sdk/lib
LDLIBS			= -lc -lgcc -lhal -lpp -lphy -lnet80211 -llwip -lwpa -lmain -lssc

OBJS			= user_main.o
HEADERS			= ap_auth.h esp-missing-decls.h uart.h uart_register.h user_config.h
FW				= fw.elf
FW1				= fw-0x00000.bin
FW2				= fw-0x40000.bin

V ?= $(VERBOSE)
ifeq ("$(V)","1")
	Q :=
	vecho := @true
else
	Q := @
	vecho := @echo
endif

.PHONY:	all

all:			$(FW1) $(FW2)

flash:			$(FW1) $(FW2)
				esptool.py --port /dev/ttyUSB1 --baud 460800 write_flash 0x00000 $(FW1) 0x40000 $(FW2)

clean:
				$(vecho) "CLEAN"
				$(Q) rm -f $(OBJS) $(FW) $(FW1) $(FW2)

user_main.o:	$(HEADERS)

$(FW1):			$(FW)
				$(vecho) "FW1 $@"
				$(Q) esptool.py elf2image $(FW)
				$(Q) mv $(FW)-0x00000.bin $(FW1)
				$(Q) -mv $(FW)-0x40000.bin $(FW2)

$(FW2):			$(FW)
				$(vecho) "FW2 $@"
				$(Q) esptool.py elf2image $(FW)
				$(Q) -mv $(FW)-0x00000.bin $(FW1)
				$(Q) mv $(FW)-0x40000.bin $(FW2)

$(FW):			$(OBJS)
				$(vecho) "LD $@"
				$(Q) xtensa-lx106-elf-gcc $(LDSDK) $(LDSCRIPT) $(LDFLAGS) -Wl,--start-group $(LDLIBS) $(OBJS) -Wl,--end-group -o $@

%.o:			%.c
				$(vecho) "CC $<"
				$(Q) xtensa-lx106-elf-gcc $(CINC) $(CFLAGS) -c $< -o $@
