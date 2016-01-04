SDKROOT				= /nfs/src/esp/opensdk
CC					= $(SDKROOT)/xtensa-lx106-elf/bin/xtensa-lx106-elf-gcc
OBJCOPY				= $(SDKROOT)/xtensa-lx106-elf/bin/xtensa-lx106-elf-objcopy
ESPTOOL				= ~/bin/esptool

LD_ADDRESS_PLAIN	= 0x40210000
LD_ADDRESS_RBOOT	= 0x40202010
LDSCRIPT_TEMPLATE	= loadscript-template
LDSCRIPT_PLAIN		= loadscript-plain
LDSCRIPT_RBOOT		= loadscript-rboot
ELF_PLAIN			= espiobridge-plain.o
ELF_RBOOT			= espiobridge-rboot.o
OFFSET_IRAM_PLAIN	= 0x00000
OFFSET_IROM_PLAIN	= 0x10000
OFFSET_BOOT_RBOOT	= 0x00000
OFFSET_CONFIG_RBOOT	= 0x01000
OFFSET_IMG_RBOOT	= 0x02000
FIRMWARE_PLAIN_IRAM	= espiobridge-plain-iram-$(OFFSET_IRAM_PLAIN).bin
FIRMWARE_PLAIN_IROM	= espiobridge-plain-irom-$(OFFSET_IROM_PLAIN).bin
FIRMWARE_RBOOT_BOOT	= espiobridge-rboot-boot.bin
FIRMWARE_RBOOT_IMG	= espiobridge-rboot-image.bin
CONFIG_RBOOT_SRC	= rboot-config.c
CONFIG_RBOOT_ELF	= rboot-config.o
CONFIG_RBOOT_BIN	= rboot-config.bin
ESPTOOL2			= ./esptool2
RBOOT				= ./rboot
LINKMAP				= linkmap
RBOOT_BIG_FLASH		= 0
RBOOT_SPI_SIZE		= 512K
ESPTOOL_SPI_SIZE	= 4m
RBOOT_SPI_MODE		= qio

CFLAGS			= -Wall -Wextra -Werror -Wformat=2 -Wuninitialized -Wno-pointer-sign -Wno-unused-parameter \
					-Wsuggest-attribute=const -Wsuggest-attribute=pure -Wno-div-by-zero -Wfloat-equal \
					-Wno-declaration-after-statement -Wundef -Wshadow -Wframe-larger-than=384 \
					-Wpointer-arith -Wbad-function-cast -Wcast-qual -Wcast-align -Wwrite-strings -Wsequence-point \
					-Wclobbered -Wlogical-op -Wold-style-definition -Wstrict-prototypes \
					-Wmissing-prototypes -Wmissing-field-initializers -Wpacked -Wredundant-decls -Wnested-externs \
					-Wlong-long -Wvla -Wdisabled-optimization -Wunreachable-code -Wtrigraphs -Wreturn-type \
					-Wmissing-braces -Wparentheses -Wimplicit -Winit-self -Wformat-nonliteral -Wcomment \
					-O3 -nostdlib -mlongcalls -mtext-section-literals -ffunction-sections -fdata-sections -D__ets__ -DICACHE_FLASH
CINC			= -I$(SDKROOT)/lx106-hal/include -I$(SDKROOT)/xtensa-lx106-elf/xtensa-lx106-elf/include \
					-I$(SDKROOT)/xtensa-lx106-elf/xtensa-lx106-elf/sysroot/usr/include -isystem$(SDKROOT)/sdk/include -I.
LDFLAGS			= -Wl,--gc-sections -Wl,-Map=$(LINKMAP) -nostdlib -Wl,--no-check-sections -u call_user_start -Wl,-static
LDSDK			= -L$(SDKROOT)/sdk/lib
LDLIBS			= -lc -lgcc -lhal -lpp -lphy -lnet80211 -llwip -lwpa -lmain -lpwm -lcrypto

OBJS			= application.o config.o display.o gpios.o i2c.o i2c_sensor.o queue.o stats.o uart.o user_main.o util.o
HEADERS			= esp-uart-register.h \
				  application.h application-parameters.h config.h display.h gpios.h i2c.h i2c_sensor.h stats.h queue.h uart.h user_main.h user_config.h

V ?= $(VERBOSE)
ifeq ("$(V)","1")
	Q :=
	VECHO := @true
	MAKEMINS :=
else
	Q := @
	VECHO := @echo
	MAKEMINS := -s
endif

section_free	= $(Q) perl -e '\
						open($$fd, "xtensa-lx106-elf-size -A $(1) |"); \
						$$available = $(5) * 1024; \
						$$used = 0; \
						while(<$$fd>) \
						{ \
							chomp; \
							@_ = split; \
							if(($$_[0] eq "$(3)") || ($$_[0] eq "$(4)")) \
							{ \
								$$used += $$_[1]; \
							} \
						} \
						$$free = $$available - $$used; \
						printf("    %-8s available: %3u k, used: %6u, free: %6u, %2u %%\n", "$(2)" . ":", $$available / 1024, $$used, $$free, 100 * $$free / $$available); \
						close($$fd);'

link_debug		= $(Q) perl -e '\
						open($$fd, "< $(1)"); \
						$$top = 0; \
						while(<$$fd>) \
						{ \
							chomp; \
							if(m/^\s+\.$(2)/) \
							{ \
								@_ = split; \
								$$top = hex($$_[1]) if(hex($$_[1]) > $$top); \
								if((hex($$_[2]) > 0) && !m/\.a\(/) \
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
						printf("size: %u, free: %u\n", $$top - hex('$(4)'), ($(3) * 1024) - ($$top - hex('$(4)'))); \
						close($$fd);'

.PHONY:	all plain rboot clean free linkdebug

all:			$(FIRMWARE_PLAIN_IRAM) $(FIRMWARE_PLAIN_IROM) $(FIRMWARE_RBOOT_BOOT) $(FIRMWARE_RBOOT_IMG) $(CONFIG_RBOOT_BIN) free
				$(VECHO) "DONE"

clean:
				$(VECHO) "CLEAN"
				$(Q) $(MAKE) $(MAKEMINS) -C $(ESPTOOL2) clean
				$(Q) $(MAKE) $(MAKEMINS) -C $(RBOOT) clean
				$(Q) rm -f $(OBJS) $(ELF_PLAIN) $(FIRMWARE_PLAIN_IRAM) $(FIRMWARE_PLAIN_IROM) $(FIRMWARE_RBOOT_BOOT) $(FIRMWARE_RBOOT_IMG)
				$(Q) rm -f $(ZIP) $(LINKMAP) $(LDSCRIPT_PLAIN) $(LDSCRIPT_RBOOT) $(CONFIG_RBOOT_ELF) $(CONFIG_RBOOT_BIN)

free:			$(ELF_PLAIN)
				$(VECHO) "MEMORY USAGE"
				$(call section_free,$(ELF_PLAIN),iram,.text,,32)
				$(call section_free,$(ELF_PLAIN),dram,.bss,.data,80)
				$(call section_free,$(ELF_PLAIN),irom,.rodata,.irom0.text,424)

linkdebug:		$(LINKMAP)
				$(Q) echo "IROM:"
				$(call link_debug,$<,irom0.text,424,40210000)
				$(Q) echo "IRAM:"
				$(call link_debug,$<,text,32,40100000)


i2c.o:			$(HEADERS)
util.o:			$(HEADERS)
gpios.o:		$(HEADERS)
i2c_sensor.o:	$(HEADERS)
display.o:		$(HEADERS)
queue.o:		queue.h
config.o:		$(HEADERS)
application.o:	$(HEADERS)
user_main.o:	$(HEADERS)
uart.o:			$(HEADERS)
stats.o:		$(HEADERS)
$(LINKMAP):		$(ELF_PLAIN)

$(ESPTOOL2)/esptool2:
						$(VECHO) "MAKE ESPTOOL2"
						$(Q) $(MAKE) $(MAKEMINS) -C $(ESPTOOL2)

$(RBOOT)/firmware/rboot.bin:	$(ESPTOOL2)/esptool2
						$(VECHO) "MAKE RBOOT"
						$(Q) $(MAKE) $(MAKEMINS) -C $(RBOOT) RBOOT_BIG_FLASH=$(RBOOT_BIG_FLASH) SPI_SIZE=$(RBOOT_SPI_SIZE) SPI_MODE=$(RBOOT_SPI_MODE)

$(LDSCRIPT_PLAIN):		$(LDSCRIPT_TEMPLATE)
						$(VECHO) "LINKER SCRIPT $@"
						$(Q) sed -e 's/@IROM0_SEG_ADDRESS@/$(LD_ADDRESS_PLAIN)/' < $< > $@

$(LDSCRIPT_RBOOT):		$(LDSCRIPT_TEMPLATE)
						$(VECHO) "LINKER SCRIPT $@"
						$(Q) sed -e 's/@IROM0_SEG_ADDRESS@/$(LD_ADDRESS_RBOOT)/' < $< > $@

$(ELF_PLAIN):			$(OBJS) $(LDSCRIPT_PLAIN)
						$(VECHO) "LD $@"
						$(Q) $(CC) $(LDSDK) -T./$(LDSCRIPT_PLAIN) $(LDFLAGS) -Wl,--start-group $(LDLIBS) $(OBJS) -Wl,--end-group -o $@

$(ELF_RBOOT):			$(OBJS) $(LDSCRIPT_RBOOT)
						$(VECHO) "LD $@"
						$(Q) $(CC) $(LDSDK) -T./$(LDSCRIPT_RBOOT) $(LDFLAGS) -Wl,--start-group $(LDLIBS) $(OBJS) -Wl,--end-group -o $@

$(FIRMWARE_PLAIN_IRAM):	$(ELF_PLAIN) $(ESPTOOL2)/esptool2
						$(VECHO) "PLAIN FIRMWARE IRAM $@"
						$(Q) $(ESPTOOL2)/esptool2 -quiet -bin -boot0 $< $@ .text .data .rodata

$(FIRMWARE_PLAIN_IROM):	$(ELF_PLAIN) $(ESPTOOL2)/esptool2
						$(VECHO) "PLAIN FIRMWARE IROM $@"
						$(Q) $(ESPTOOL2)/esptool2 -quiet -lib $< $@

$(FIRMWARE_RBOOT_BOOT):	$(RBOOT)/firmware/rboot.bin
						cp $< $@

$(FIRMWARE_RBOOT_IMG):	$(ELF_RBOOT) $(ESPTOOL2)/esptool2
						$(VECHO) "RBOOT FIRMWARE $@"
						$(Q) $(ESPTOOL2)/esptool2 -quiet -bin -boot2 $< $@ .text .data .rodata

$(CONFIG_RBOOT_BIN):	$(CONFIG_RBOOT_ELF)
						$(VECHO) "RBOOT CONFIG $@"
						$(Q) $(OBJCOPY) --output-target binary $< $@

plain:					$(FIRMWARE_PLAIN_IRAM) $(FIRMWARE_PLAIN_IROM) free
						$(Q) $(ESPTOOL) write_flash --flash_size $(ESPTOOL_SPI_SIZE) $(OFFSET_IRAM_PLAIN) $(FIRMWARE_PLAIN_IRAM) $(OFFSET_IROM_PLAIN) $(FIRMWARE_PLAIN_IROM)

rboot:					$(FIRMWARE_RBOOT_BOOT) $(CONFIG_RBOOT_BIN) $(FIRMWARE_RBOOT_IMG) free
						$(Q) $(ESPTOOL) write_flash --flash_size $(ESPTOOL_SPI_SIZE) $(OFFSET_BOOT_RBOOT) $(FIRMWARE_RBOOT_BOOT) $(OFFSET_CONFIG_RBOOT) $(CONFIG_RBOOT_BIN) $(OFFSET_IMG_RBOOT) $(FIRMWARE_RBOOT_IMG)

%.o:					%.c
						$(VECHO) "CC $<"
						$(Q) $(CC) $(CINC) $(CFLAGS) -c $< -o $@
