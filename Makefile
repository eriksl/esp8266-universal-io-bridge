SDKROOT			= /nfs/src/esp/opensdk
SDKLD			= $(SDKROOT)/sdk/ld

LINKMAP			= linkmap

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
LDSCRIPT		= -T./eagle.app.v6.ld
LDSDK			= -L$(SDKROOT)/sdk/lib
LDLIBS			= -lc -lgcc -lhal -lpp -lphy -lnet80211 -llwip -lwpa -lmain -lpwm -lcrypto

OBJS			= application.o config.o display.o gpios.o i2c.o i2c_sensor.o queue.o stats.o uart.o user_main.o util.o
HEADERS			= esp-uart-register.h \
				  application.h application-parameters.h config.h display.h gpios.h i2c.h i2c_sensor.h stats.h queue.h uart.h user_main.h user_config.h

ELF				= espiobridge.elf
ADDR_IRAM		= 0x00000
ADDR_IROM		= 0x10000
FILE_IRAM		= espiobridge-iram-$(ADDR_IRAM).bin
FILE_IROM		= espiobridge-irom-$(ADDR_IROM).bin
ESPTOOL2		= ./esptool2
RBOOT			= ./rboot

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
							if($$_[0] eq "$(3)") \
							{ \
								$$total = $(4) * 1024; \
								$$used = $$_[1]; \
								$$left = $$total - $$used; \
								printf("%-8s available: %3u k, used: %3u k, free: %6u\n", "$(2)" . ":", $$total / 1024, $$used / 1024, $$left); \
							} \
						} \
						close($$fd);'

section2_free	= $(Q) perl -e '\
						open($$fd, "< linkmap"); \
						while(<$$fd>) \
						{ \
							chomp(); \
							($$end_address) = m/\s+(0x[0-9a-f]+)\s+$(3) = ABSOLUTE \(.\)/; \
							if(defined($$end_address)) \
							{ \
								$$start_address = $(4); \
								$$end_address = hex($$end_address); \
								$$used = $$end_address - $$start_address; \
								$$available = $(5); \
								$$free = $$available - $$used; \
								printf("%-8s available: %3u k, used: %3u k, free: %6d\n", "$(1)" . ":", $$available / 1024, $$used / 1024, $$free); \
							} \
						} \
						close($$fd);'

file_free =		$(Q) perl -e '\
					$$iram = (-s "$(FILE_IRAM)") / 1024; \
					$$irom = (-s "$(FILE_IROM)") / 1024; \
					printf("file size: iram: %u k, irom: %u k, both: %u k, free: %u k\n", $$iram, $$irom, $$all, $(1) - $$all);'

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
						printf("size: %u, free: %u\n", $$top - hex('$(4)'), ($(3) * 1024) - ($$top - hex('$(4)'))); \
						close($$fd);'

.PHONY:	all flash clean free linkdebug

all:			$(FILE_IRAM) $(FILE_IROM)
#				$(call section_free,$(ELF),.bss,80)
#				$(call section_free,$(ELF),.data,80)
#				$(call section_free,$(ELF),.rodata,80)
				$(call section_free,$(ELF),iram,.text,32)
				$(call section_free,$(ELF),irom,.irom0.text,424)
				$(call file_free, 456)

$(ESPTOOL2)/esptool2:
				$(vecho) "MAKE ESPTOOL2"
				$(Q) $(MAKE) -C $(ESPTOOL2)

$(RBOOT)/firmware/rboot.bin:
				$(vecho) "MAKE RBOOT"
				$(Q) $(MAKE) -C $(RBOOT)

clean:
				$(vecho) "CLEAN"
				$(Q) $(MAKE) -C $(ESPTOOL2) clean > /dev/null 2>&1
				$(Q) $(MAKE) -C $(RBOOT) clean > /dev/null 2>&1
				$(Q) rm -f $(OBJS) $(ELF) $(FILE_IRAM) $(FILE_IROM) $(ZIP) $(LINKMAP)

free:			$(LINKMAP) $(FILE_IRAM) $(FILE_IROM)
#				$(call section2_free,dram,dram0,_bss_end,0x3ffe8000,0x14000)
				$(call section2_free,iram,iram1,_lit4_end,0x40100000,0x8000)
				$(call section2_free,irom,irom0,_irom0_text_end,0x40210000,0x6a000)

linkdebug:		$(OBJS)
				$(Q) xtensa-lx106-elf-gcc $(LDSDK) $(LDSCRIPT) $(LDFLAGS) -Wl,--start-group $(LDLIBS) $(OBJS) -Wl,--end-group -o $@
				$(Q) echo "IROM:"
				$(call link_debug, $(LINKMAP),irom0.text,424,40210000)
				$(Q) echo "IRAM:"
				$(call link_debug, $(LINKMAP),text,32,40100000)

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

$(ELF):			$(OBJS)
				$(vecho) "LD $@"
				$(Q) xtensa-lx106-elf-gcc $(LDSDK) $(LDSCRIPT) $(LDFLAGS) -Wl,--start-group $(LDLIBS) $(OBJS) -Wl,--end-group -o $@

$(FILE_IRAM):	$(ELF) $(ESPTOOL2)/esptool2
				$(vecho) "SEGMENT IRAM"
				$(Q) $(ESPTOOL2)/esptool2 -quiet -bin -boot0 $< $@ .text .data .rodata

$(FILE_IROM):	$(ELF) $(ESPTOOL2)/esptool2
				$(vecho) "SEGMENT IROM"
				$(Q) $(ESPTOOL2)/esptool2 -quiet -lib $< $@

flash:			$(FILE_IRAM) $(FILE_IROM)
				$(Q) esptool write_flash $(ADDR_IRAM) $(FILE_IRAM) $(ADDR_IROM) $(FILE_IROM)

$(LINKMAP):		$(ELF)

%.o:			%.c
				$(vecho) "CC $<"
				$(Q) xtensa-lx106-elf-gcc $(CINC) $(CFLAGS) -c $< -o $@
