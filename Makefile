SDKROOT			= /nfs/src/esp/opensdk
SDKLD			= $(SDKROOT)/sdk/ld

LINKMAP			= linkmap

CFLAGS			= -O3 -Wall -Wno-pointer-sign -nostdlib -mlongcalls -mtext-section-literals  -D__ets__ -DICACHE_FLASH
CINC			= -I$(SDKROOT)/lx106-hal/include -I$(SDKROOT)/xtensa-lx106-elf/xtensa-lx106-elf/include \
					-I$(SDKROOT)/xtensa-lx106-elf/xtensa-lx106-elf/sysroot/usr/include -I$(SDKROOT)/sdk/include -I.
LDFLAGS			= -Wl,-Map=$(LINKMAP) -nostdlib -Wl,--no-check-sections -u call_user_start -Wl,-static
LDSCRIPT		= -T./eagle.app.v6.ld
LDSDK			= -L$(SDKROOT)/sdk/lib
LDLIBS			= -lc -lgcc -lhal -lpp -lphy -lnet80211 -llwip -lwpa -lmain -lpwm

OBJS			= application.o config.o gpios.o i2c.o i2c_sensor.o queue.o stats.o uart.o user_main.o util.o
HEADERS			= esp-uart-register.h \
				  application.h application-parameters.h config.h gpios.h i2c.h i2c_sensor.h stats.h queue.h uart.h user_main.h user_config.h
FW				= fw.elf
FW1				= fw-0x00000.bin
FW2				= fw-0x40000.bin
ZIP				= espbasicbridge.zip 

V ?= $(VERBOSE)
ifeq ("$(V)","1")
	Q :=
	vecho := @true
else
	Q := @
	vecho := @echo
endif

segment_free	= $(Q) perl -e '\
						open($$fd, "xtensa-lx106-elf-size -A $(1) |"); \
						while(<$$fd>) \
						{ \
							chomp; \
							@_ = split; \
							if($$_[0] eq "$(2)") \
							{ \
								$$total = $(3) * 1024; \
								$$used = $$_[1]; \
								$$left = $$total - $$used; \
								printf("%-12s avail: %6d, used: %6d, free: %6d\n", "$(2)" . ":", $$total, $$used, $$left); \
							} \
						} \
						close($$fd);'

link_debug		= $(Q) perl -e '\
						open($$fd, "< $(1)"); \
						$$top = 0; \
						while(<$$fd>) \
						{ \
							chomp; \
							if(/^\s+\.$(2)/) \
							{ \
								@_ = split; \
								$$top = hex($$_[1]) if(hex($$_[1]) > $$top); \
								if(hex($$_[2]) > 0) \
								{ \
									$$size = sprintf("%06x", hex($$_[2])); \
									$$file = $$_[3]; \
									$$file =~ s/.*\///g; \
									$$size{"$$size-$$file"} = { size => $$size, id => $$file}; \
								} \
							} \
						} \
						for $$size (sort(keys(%size))) \
						{ \
							printf("%4d: %s\n", \
									hex($$size{$$size}{"size"}), \
									$$size{$$size}{"id"}); \
						} \
						printf("size: %d, free: %d\n", $$top - hex('40100000'), ($(3) * 1024) - ($$top - hex('40100000'))); \
						close($$fd);'

.PHONY:	all reset flash zip linkdebug

all:			$(FW1) $(FW2)
				$(call segment_free,$(FW),.irom0.text,240)
				$(call segment_free,$(FW),.text,32)

zip:			all
				$(Q) zip -9 $(ZIP) $(FW1) $(FW2) LICENSE README.md

flash:			all
				$(Q) espflash $(FW1) $(FW2)

clean:
				$(vecho) "CLEAN"
				$(Q) rm -f $(OBJS) $(FW) $(FW1) $(FW2) $(ZIP)

linkdebug:		$(OBJS)
				-$(Q) xtensa-lx106-elf-gcc $(LDSDK) $(LDSCRIPT) $(LDFLAGS) -Wl,--start-group $(LDLIBS) $(OBJS) -Wl,--end-group -o $@
				$(call link_debug, $(LINKMAP),text,32)

config.o:		$(HEADERS)
gpio.o:			$(HEADERS)
i2c.o:			$(HEADERS)
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
