SDKROOT			= /nfs/src/esp/opensdk
SDKLD			= $(SDKROOT)/sdk/ld

LINKMAP			= linkmap

CFLAGS			= -Wall -Wextra -Werror -Wformat=2 -Wuninitialized -Wno-pointer-sign -Wno-unused-parameter \
					-Wsuggest-attribute=const -Wsuggest-attribute=pure -Wno-div-by-zero -Wfloat-equal \
					-Wno-declaration-after-statement -Wundef -Wshadow -Wframe-larger-than=256 \
					-Wpointer-arith -Wbad-function-cast -Wcast-qual -Wcast-align -Wwrite-strings -Wsequence-point \
					-Wclobbered -Wlogical-op -Waggregate-return -Wold-style-definition -Wstrict-prototypes \
					-Wmissing-prototypes -Wmissing-field-initializers -Wpacked -Wredundant-decls -Wnested-externs \
					-Wlong-long -Wvla -Wdisabled-optimization -Wunreachable-code -Wtrigraphs -Wreturn-type \
					-Wmissing-braces -Wparentheses -Wimplicit -Winit-self -Wformat-nonliteral -Wcomment \
					-O3 -nostdlib -mlongcalls -mtext-section-literals -ffunction-sections -fdata-sections -D__ets__ -DICACHE_FLASH
CINC			= -I$(SDKROOT)/lx106-hal/include -I$(SDKROOT)/xtensa-lx106-elf/xtensa-lx106-elf/include \
					-I$(SDKROOT)/xtensa-lx106-elf/xtensa-lx106-elf/sysroot/usr/include -isystem$(SDKROOT)/sdk/include -I.
LDFLAGS			= -Wl,--gc-sections -Wl,-Map=$(LINKMAP) -nostdlib -Wl,--no-check-sections -u call_user_start -Wl,-static
LDSCRIPT		= -T./eagle.app.v6.ld
LDSDK			= -L$(SDKROOT)/sdk/lib
LDLIBS			= -lc -lgcc -lhal -lpp -lphy -lnet80211 -llwip -lwpa -lmain -lpwm

OBJS			= application.o config.o gpios.o i2c.o i2c_sensor.o queue.o stats.o uart.o user_main.o util.o
HEADERS			= esp-uart-register.h \
				  application.h application-parameters.h config.h gpios.h i2c.h i2c_sensor.h stats.h queue.h uart.h user_main.h user_config.h

FW1A			= 0x00000
FW2A			= 0x10000
FW				= fw.elf
FW1				= fw-$(FW1A).bin
FW2				= fw-$(FW2A).bin
ZIP				= espiobridge.zip

V ?= $(VERBOSE)
ifeq ("$(V)","1")
	Q :=
	vecho := @true
else
	Q := @
	vecho := @echo
endif

section_free	= $(Q) perl -e '\
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

section2_free	= $(Q) perl -e '\
						open($$fd, "< linkmap"); \
						while(<$$fd>) \
						{ \
							chomp(); \
							($$end_address) = m/\s+(0x[0-9a-f]+)\s+$(2) = ABSOLUTE \(.\)/; \
							if(defined($$end_address)) \
							{ \
								$$start_address = $(3); \
								$$end_address = hex($$end_address); \
								$$used = $$end_address - $$start_address; \
								$$available = $(4); \
								$$free = $$available - $$used; \
								printf("%-6s available: %6u, used: %6u, free: %6d\n", "$(1)", $$available, $$used, $$free); \
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
						printf("size: %d, free: %d\n", $$top - hex('$(4)'), ($(3) * 1024) - ($$top - hex('$(4)'))); \
						close($$fd);'

.PHONY:	all reset flash zip linkdebug free

all:			$(FW1) $(FW2)
#				$(call section_free,$(FW),.bss,80)
#				$(call section_free,$(FW),.data,80)
#				$(call section_free,$(FW),.rodata,80)
				$(call section_free,$(FW),.irom0.text,424)
				$(call section_free,$(FW),.text,32)
				$(Q) perl -e '$$fw1size = (-s "$(FW1)") / 1024 + 1; $$fw2size = (-s "$(FW2)") / 1024 + 1; printf("FW1: %u k, FW2: %u k, both: %u k, free: %u k\n", $$fw1size, $$fw2size, $$fw1size + $$fw2size, 424 - ($$fw1size + $$fw2size));'

zip:			all
				$(Q) zip -9 $(ZIP) $(FW1) $(FW2) LICENSE README.md

flash:			all
				$(Q) esptool write_flash $(FW1A) $(FW1) $(FW2A) $(FW2)

clean:
				$(vecho) "CLEAN"
				$(Q) rm -f $(OBJS) $(FW) $(FW1) $(FW2) $(ZIP) $(LINKMAP)

free:			$(LINKMAP) $(FW1) $(FW2)
				$(call section2_free,dram0,_bss_end,0x3ffe8000,0x14000)
				$(call section2_free,iram1,_lit4_end,0x40100000,0x8000)
				$(call section2_free,irom0,_irom0_text_end,0x40210000,0x6a000)

linkdebug:		$(OBJS)
				$(Q) xtensa-lx106-elf-gcc $(LDSDK) $(LDSCRIPT) $(LDFLAGS) -Wl,--start-group $(LDLIBS) $(OBJS) -Wl,--end-group -o $@
				$(Q) echo "IROM:"
				$(call link_debug, $(LINKMAP),irom0.text,224,40240000)
				$(Q) echo "IRAM:"
				$(call link_debug, $(LINKMAP),text,32,40100000)

config.o:		$(HEADERS)
gpio.o:			$(HEADERS)
i2c.o:			$(HEADERS)
user_main.o:	$(HEADERS)

$(FW1):			$(FW)
				$(vecho) "FW1 $@"
				$(Q) esptool.py elf2image $(FW)
				$(Q) mv $(FW)-$(FW1A).bin $(FW1)
				$(Q) -mv $(FW)-$(FW2A).bin $(FW2)

$(FW2):			$(FW)
				$(vecho) "FW2 $@"
				$(Q) esptool.py elf2image $(FW)
				$(Q) -mv $(FW)-$(FW1A).bin $(FW1)
				$(Q) mv $(FW)-$(FW2A).bin $(FW2)

$(FW):			$(OBJS)
				$(vecho) "LD $@"
				$(Q) xtensa-lx106-elf-gcc $(LDSDK) $(LDSCRIPT) $(LDFLAGS) -Wl,--start-group $(LDLIBS) $(OBJS) -Wl,--end-group -o $@

$(LINKMAP):		$(FW)

%.o:			%.c
				$(vecho) "CC $<"
				$(Q) xtensa-lx106-elf-gcc $(CINC) $(CFLAGS) -c $< -o $@
