IMAGE				?= ota
ESPTOOL				?= ~/bin/esptool
HOSTCC				?= gcc
HOSTCPP				?= g++
OTA_HOST			?= 10.1.12.222
OTA_FLASH			?= ./espif
SPI_FLASH_MODE		?= qio
# using LTO will sometimes yield some extra bytes of IRAM, but it
# takes longer to compile and the linker map will become useless
USE_LTO				?= 0

# no user serviceable parts below

MAKEFLAGS += --no-builtin-rules

GIT_COMMIT					:= "\"`export LC_ALL=C; git log -n 1 --oneline`\""
ARCH						:= xtensa-lx106-elf
THIRDPARTY					:= $(PWD)/third-party
ESPSDK						:= $(THIRDPARTY)/ESP8266_NONOS_SDK
ESPSDK_LIB					:= $(ESPSDK)/lib
ESPTOOL2					:= $(THIRDPARTY)/esptool2
RBOOT						:= $(THIRDPARTY)/rboot

LWIP						:= $(THIRDPARTY)/lwip-for-esp8266-nonos-sdk
LWIP_SRC					:= $(LWIP)/src
LWIP_SYSROOT				:= $(LWIP)/$(ARCH)
LWIP_SYSROOT_LIB			:= $(LWIP_SYSROOT)/lib
LWIP_LIB					:= lwip
LWIP_LIB_FILE				:= $(LWIP_SYSROOT_LIB)/lib$(LWIP_LIB).a

LWIP_ESPRESSIF				:= $(THIRDPARTY)/lwip-espressif-contributed
LWIP_ESPRESSIF_SYSROOT		:= $(LWIP_ESPRESSIF)/$(ARCH)
LWIP_ESPRESSIF_SYSROOT_LIB	:= $(LWIP_ESPRESSIF_SYSROOT)/lib
LWIP_ESPRESSIF_LIB			:= lwip-espressif
LWIP_ESPRESSIF_LIB_FILE		:= $(LWIP_ESPRESSIF_SYSROOT_LIB)/lib$(LWIP_ESPRESSIF_LIB).a

CTNG						:= $(THIRDPARTY)/crosstool-ng
CTNG_LX106					:= $(CTNG)/samples/$(ARCH)
CTNG_SYSROOT				:= $(CTNG)/$(ARCH)
CTNG_SYSROOT_INCLUDE		:= $(CTNG_SYSROOT)/$(ARCH)/include
CTNG_SYSROOT_LIB			:= $(CTNG_SYSROOT)/$(ARCH)/lib
CTNG_SYSROOT_BIN			:= $(CTNG_SYSROOT)/bin

CC							:= $(CTNG_SYSROOT_BIN)/$(ARCH)-gcc
OBJCOPY						:= $(CTNG_SYSROOT_BIN)/$(ARCH)-objcopy
SIZE						:= $(CTNG_SYSROOT_BIN)/$(ARCH)-size

USER_CONFIG_SECTOR			:= 0xfa
USER_CONFIG_SIZE			:= 0x1000
SEQUENCER_FLASH_OFFSET_0	:= 0x0f6000
SEQUENCER_FLASH_OFFSET_1	:= 0x1f6000
SEQUENCER_FLASH_SIZE		:= 0x4000
FONT_FLASH_OFFSET_0			:= 0x0d6000
FONT_FLASH_OFFSET_1			:= 0x1d6000
FONT_FLASH_SIZE				:= 0x20000
PICTURE_FLASH_OFFSET_0		:= 0x200000
PICTURE_FLASH_OFFSET_1		:= 0x280000
PICTURE_FLASH_SIZE			:= 0x80000
RFCAL_OFFSET				:= 0xfb000
RFCAL_SIZE					:= 0x1000
RFCAL_FILE					:= blank1.bin
PHYDATA_OFFSET				:= 0x1fc000
PHYDATA_SIZE				:= 0x1000
PHYDATA_FILE				:= esp_init_data_default_v08.bin
SYSTEM_CONFIG_OFFSET		:= 0x1fd000
SYSTEM_CONFIG_SIZE			:= 0x3000
SYSTEM_CONFIG_FILE			:= blank3.bin

LDSCRIPT_TEMPLATE			:= loadscript-template
LDSCRIPT					:= loadscript
ELF_IMAGE					:= espiobridge-rboot.o
OFFSET_BOOT					:= 0x000000
SIZE_BOOT					:= 0x1000
OFFSET_RBOOT_CFG			:= 0x001000
SIZE_RBOOT_CFG				:= 0x1000
OFFSET_IMG_0				:= 0x002000
OFFSET_IMG_1				:= 0x102000
SIZE_IMG					:= 0x94000
MISC_FLASH_OFFSET			:= 0x300000
MISC_FLASH_SIZE				:= 0x100000
FIRMWARE_RBOOT				:= espiobridge-rboot-boot.bin
FIRMWARE_IMG				:= espiobridge-rboot-image.bin
CONFIG_RBOOT_SRC			:= rboot-config.c
CONFIG_RBOOT_ELF			:= rboot-config.o
CONFIG_RBOOT_BIN			:= rboot-config.bin
CONFIG_DEFAULT_BIN			:= default-config.bin
CONFIG_BACKUP_BIN			:= backup-config.bin
LINKMAP						:= linkmap
MEMORY_USAGE_OUTPUT			:= memory
MEMORY_USAGE_LOG			:= memory-log
LIBMAIN_ORIGINAL			:= main
LIBMAIN_ORIGINAL_FILE		:= $(ESPSDK_LIB)/lib$(LIBMAIN_ORIGINAL).a
LIBMAIN_RBB					:= main_rbb
LIBMAIN_RBB_FILE			:= lib$(LIBMAIN_RBB).a
ESPTOOL2_BIN				:= $(ESPTOOL2)/esptool2
RBOOT_BIN					:= $(RBOOT)/firmware/rboot.bin
# this must be 4 MB otherwise we can't access the part in flash above 2 MB when using a 4 MB flash
# only 2 MB is actually used for image
FLASH_SIZE_ESPTOOL			:= 4MB-c1
FLASH_SIZE_ESPTOOL2			:= 4096
FLASH_SIZE_SDK				:= FLASH_SIZE_32M_MAP_1024_1024
RBOOT_SPI_SIZE				:= 4Mb
USER_CONFIG_OFFSET			:= $(USER_CONFIG_SECTOR)000
LD_ADDRESS					:= 0x40202010
LD_LENGTH					:= 0xf7ff0

V ?= $(VERBOSE)
ifeq ($(V),1)
	Q :=
	VECHO := @true
	MAKEMINS :=
else
	Q := @
	VECHO := @echo
	MAKEMINS := -s
endif

ALL_IMAGE_TARGETS	:= $(FIRMWARE_RBOOT) $(CONFIG_RBOOT_BIN) $(FIRMWARE_IMG)
ALL_BUILD_TARGET	:= ctng lwip lwip_espressif
ALL_FLASH_TARGETS	:= espflash espif
ALL_TOOL_TARGETS	:= resetserial
ALL_EXTRA_TARGETS	:= free

CCWARNINGS			:=	-Wall -Wextra -Werror \
						-Wformat-overflow=2 -Wshift-overflow=2 -Wimplicit-fallthrough=5 \
						-Wformat-signedness -Wformat-truncation=2 \
						-Wstringop-overflow=4 -Wunused-const-variable=2 -Walloca \
						-Warray-bounds=2 -Wswitch-bool -Wsizeof-array-argument \
						-Wduplicated-branches -Wduplicated-cond -Wlto-type-mismatch -Wnull-dereference \
						-Wdangling-else \
						-Wpacked -Wfloat-equal -Winit-self -Wmissing-include-dirs \
						-Wmissing-noreturn -Wbool-compare \
						-Wsuggest-attribute=noreturn -Wsuggest-attribute=format -Wmissing-format-attribute \
						-Wuninitialized -Wtrampolines -Wframe-larger-than=2048 \
						-Wunsafe-loop-optimizations -Wshadow -Wpointer-arith -Wbad-function-cast \
						-Wcast-qual -Wwrite-strings -Wsequence-point -Wlogical-op -Wlogical-not-parentheses \
						-Wredundant-decls -Wvla -Wdisabled-optimization \
						-Wunreachable-code -Wparentheses -Wdiscarded-array-qualifiers \
						-Wmissing-prototypes -Wold-style-definition -Wold-style-declaration -Wmissing-declarations \
						-Wcast-align -Winline -Wmultistatement-macros -Warray-bounds=2 \
						\
						-Wno-error=cast-qual \
						-Wno-error=unsafe-loop-optimizations \
						\
						-Wno-packed \
						-Wno-unused-parameter \

CFLAGS			:=	-pipe -Os -g -std=gnu11 -fdiagnostics-color=always \
						-fno-inline -mlongcalls -mno-serialize-volatile -mno-target-align \
						-fno-math-errno -fno-printf-return-value \
						-ftree-vrp \
						-ffunction-sections -fdata-sections

ifeq ($(USE_LTO),1)
CFLAGS 			+=	-flto=8 -flto-compression-level=0 -fuse-linker-plugin -ffat-lto-objects -flto-partition=max
endif

CFLAGS			+=	-DBOOT_BIG_FLASH=1 -DBOOT_RTC_ENABLED=1 \
						-DGIT_COMMIT=$(GIT_COMMIT) \
						-DUSER_CONFIG_SECTOR=$(USER_CONFIG_SECTOR) -DUSER_CONFIG_OFFSET=$(USER_CONFIG_OFFSET) -DUSER_CONFIG_SIZE=$(USER_CONFIG_SIZE) \
						-DRFCAL_OFFSET=$(RFCAL_OFFSET) -DRFCAL_SIZE=$(RFCAL_SIZE) \
						-DPHYDATA_OFFSET=$(PHYDATA_OFFSET) -DPHYDATA_SIZE=$(PHYDATA_SIZE) \
						-DSYSTEM_CONFIG_OFFSET=$(SYSTEM_CONFIG_OFFSET) -DSYSTEM_CONFIG_SIZE=$(SYSTEM_CONFIG_SIZE) \
						-DOFFSET_IMG_0=$(OFFSET_IMG_0) -DOFFSET_IMG_1=$(OFFSET_IMG_1) -DSIZE_IMG=$(SIZE_IMG) \
						-DSEQUENCER_FLASH_SIZE=$(SEQUENCER_FLASH_SIZE) \
						-DSEQUENCER_FLASH_OFFSET_0=$(SEQUENCER_FLASH_OFFSET_0) -DSEQUENCER_FLASH_OFFSET_1=$(SEQUENCER_FLASH_OFFSET_1) \
						-DFONT_FLASH_SIZE=$(FONT_FLASH_SIZE) \
						-DFONT_FLASH_OFFSET_0=$(FONT_FLASH_OFFSET_0) -DFONT_FLASH_OFFSET_1=$(FONT_FLASH_OFFSET_1) \
						-DPICTURE_FLASH_SIZE=$(PICTURE_FLASH_SIZE) \
						-DPICTURE_FLASH_OFFSET_0=$(PICTURE_FLASH_OFFSET_0) -DPICTURE_FLASH_OFFSET_1=$(PICTURE_FLASH_OFFSET_1) \
						-DMISC_FLASH_SIZE=$(MISC_FLASH_SIZE) -DMISC_FLASH_OFFSET=$(MISC_FLASH_OFFSET) \
						-DOFFSET_BOOT=$(OFFSET_BOOT) -DSIZE_BOOT=$(SIZE_BOOT) \
						-DOFFSET_RBOOT_CFG=$(OFFSET_RBOOT_CFG) -DSIZE_RBOOT_CFG=$(SIZE_RBOOT_CFG) \
						-DFLASH_SIZE_SDK=$(FLASH_SIZE_SDK)

CINC			:= -I$(CTNG_SYSROOT_INCLUDE) -I$(LWIP_SRC)/include/ipv4 -I$(LWIP_SRC)/include -I$(PWD)
LDFLAGS			:= -L$(CTNG_SYSROOT_LIB) -L$(LWIP_SYSROOT_LIB) -L$(LWIP_ESPRESSIF_SYSROOT_LIB) -L$(ESPSDK_LIB) -L. -Wl,--size-opt -Wl,--print-memory-usage -Wl,--gc-sections -Wl,--cref -Wl,-Map=$(LINKMAP) -nostdlib -u call_user_start -Wl,-static
SDKLIBS			:= -lpp -lphy -lnet80211 -lwpa
LWIPLIBS		:= -l$(LWIP_LIB) -l$(LWIP_ESPRESSIF_LIB)
STDLIBS			:= -lm -lgcc -lcrypto -lc
HOSTCPPFLAGS	:= -O3 -Wall -Wextra -Werror -Wframe-larger-than=65536 -Wno-error=ignored-qualifiers \
					-DMAGICKCORE_HDRI_ENABLE=0 -DMAGICKCORE_QUANTUM_DEPTH=16 -I/usr/include/ImageMagick-6 \
					-lssl -lcrypto -lpthread -lboost_system -lboost_program_options -lboost_regex -lboost_thread -lMagick++-6.Q16

OBJS			:= application.o config.o display.o display_cfa634.o display_lcd.o display_orbital.o display_saa.o \
						display_seeed.o display_eastrising.o display_ssd1306.o display_font_6x8.o io_pcf.o http.o \
						io.o io_gpio.o io_aux.o io_mcp.o io_ledpixel.o \
						mailbox.o queue.o stats.o sys_time.o uart.o dispatch.o util.o sequencer.o init.o i2c.o i2c_sensor.o \
						lwip-interface.o sys_string.o remote_trigger.o spi.o i2s.o rboot-interface.o

LWIP_OBJS		:= $(LWIP_SRC)/core/def.o $(LWIP_SRC)/core/dhcp.o $(LWIP_SRC)/core/init.o \
						$(LWIP_SRC)/core/mem.o $(LWIP_SRC)/core/memp.o \
						$(LWIP_SRC)/core/netif.o $(LWIP_SRC)/core/pbuf.o \
						$(LWIP_SRC)/core/tcp.o $(LWIP_SRC)/core/tcp_in.o $(LWIP_SRC)/core/tcp_out.o \
						$(LWIP_SRC)/core/timers.o \
						$(LWIP_SRC)/core/udp.o \
						$(LWIP_SRC)/core/ipv4/icmp.o \
						$(LWIP_SRC)/core/ipv4/igmp.o \
						$(LWIP_SRC)/core/ipv4/inet.o $(LWIP_SRC)/core/ipv4/inet_chksum.o \
						$(LWIP_SRC)/core/ipv4/ip.o $(LWIP_SRC)/core/ipv4/ip_addr.o \
						$(LWIP_SRC)/netif/etharp.o

HEADERS			:= application.h config.h display.h display_cfa634.h display_lcd.h display_orbital.h display_saa.h \
						display_seeed.h display_eastrising.h display_font_6x8.h display_ssd1306.h \
						http.h i2c.h i2c_sensor.h io.h io_gpio.h remote_trigger.h spi.h i2s.h \
						io_aux.h io_mcp.h io_ledpixel.h io_pcf.h mailbox.h \
						queue.h stats.h uart.h user_config.h dispatch.h util.h sequencer.h init.h \
						rboot-interface.h lwip-interface.h eagle.h sdk.h

.PRECIOUS:		*.cpp *.c *.h $(CTNG)/.config.orig $(CTNG)/scripts/crosstool-NG.sh.orig
.PHONY:			all flash flash-plain flash-ota clean realclean free always ota showsymbols udprxtest tcprxtest udptxtest tcptxtest test release $(ALL_BUILD_TARGETS)

all:			$(ALL_TOOL_TARGETS) $(ALL_FLASH_TARGETS) $(ALL_IMAGE_TARGETS) $(ALL_EXTRA_TARGETS)
				$(VECHO) "DONE $(IMAGE) TARGETS $(ALL_IMAGE_TARGETS) CONFIG SECTOR $(USER_CONFIG_SECTOR)"

clean:
				$(VECHO) "CLEAN"
				$(Q) $(MAKE) $(MAKEMINS) -C $(ESPTOOL2) clean
				$(Q) $(MAKE) $(MAKEMINS) -C $(RBOOT) clean
				-$(Q) rm -f $(OBJS) \
						$(ELF_IMAGE) \
						$(FIRMWARE_RBOOT) $(FIRMWARE_IMG) \
						$(LDSCRIPT) \
						$(CONFIG_RBOOT_ELF) $(CONFIG_RBOOT_BIN) \
						$(LIBMAIN_RBB_FILE) $(ZIP) $(LINKMAP) \
						espif 2> /dev/null

realclean:		clean
				$(VECHO) "REALCLEAN"
				-$(Q) rm -f espif.h.gch resetserial espflash 2> /dev/null

free:			$(ELF_IMAGE)
				$(VECHO) "MEMORY USAGE"
				$(call section_free,$(ELF_IMAGE),iram,.text,,,32768)
				$(call section_free,$(ELF_IMAGE),dram,.bss,.data,.rodata,80265)
				$(call section_free,$(ELF_IMAGE),irom,.irom0.text,,,606208)
				$(Q) export LC_ALL=C; echo "`date` `git log -n 1 --oneline`" > $(MEMORY_USAGE_OUTPUT)
				$(call section_free,$(ELF_IMAGE),iram,.text,,,32768) >> $(MEMORY_USAGE_OUTPUT)
				$(call section_free,$(ELF_IMAGE),dram,.bss,.data,.rodata,80265) >> $(MEMORY_USAGE_OUTPUT)
				$(call section_free,$(ELF_IMAGE),irom,.irom0.text,,,606208) >> $(MEMORY_USAGE_OUTPUT)
				$(Q) rm -f .diff-a .diff-b
				$(Q) tail -n 3 $(MEMORY_USAGE_OUTPUT) > .diff-a
				$(Q) tail -n 3 $(MEMORY_USAGE_LOG) > .diff-b
				$(Q) if [ `diff .diff-a .diff-b | wc -l` -gt 0 ]; then cat $(MEMORY_USAGE_OUTPUT) >> $(MEMORY_USAGE_LOG); fi
				$(Q) rm -f .diff-a .diff-b $(MEMORY_USAGE_OUTPUT)

showsymbols:	$(ELF_IMAGE)
				./symboltable.pl $(ELF_IMAGE) 2>&1 | less

# crosstool-NG toolchain

$(CTNG_LX106)/crosstool.config:
										$(VECHO) "CROSSTOOL-NG SUBMODULE INIT"
										$(Q) git submodule init $(CTNG)
										$(Q) git submodule update $(CTNG)

$(CTNG_LX106)/crosstool.config.orig:	$(CTNG_LX106)/crosstool.config
										$(VECHO) "CROSSTOOL-NG PATCH CONFIG"
										$(Q) cp $(CTNG_LX106)/crosstool.config $(CTNG_LX106)/crosstool.config.orig
										$(Q) (cd $(CTNG_LX106); patch -p0 -i ../../../../crosstool-config.patch)
										$(Q) touch $(CTNG_LX106)/crosstool.config.orig

$(CTNG)/configure:						$(CTNG_LX106)/crosstool.config.orig
										$(VECHO) "CROSSTOOL-NG BOOTSTRAP"
										$(Q) (cd $(CTNG); ./bootstrap)

$(CTNG)/Makefile:						$(CTNG)/configure $(CTNG)/Makefile.in
										$(VECHO) "CROSSTOOL-NG CONFIGURE"
										$(Q) (cd $(CTNG); ./configure --prefix=`pwd`)

$(CTNG)/ct-ng:							$(CTNG)/Makefile $(CTNG)/ct-ng.in
										$(VECHO) "CROSSTOOL-NG MAKE"
										$(Q) $(MAKE) -C $(CTNG) MAKELEVEL=0

$(CTNG)/bin/ct-ng:						$(CTNG)/ct-ng
										$(VECHO) "CROSSTOOL-NG MAKE INSTALL"
										$(Q) $(MAKE) -C $(CTNG) install MAKELEVEL=0

$(CTNG)/.config:						$(CTNG)/bin/ct-ng
										$(VECHO) "CROSSTOOL-NG CREATE CONFIG"
										$(Q) $(MAKE) -C $(CTNG) -f ct-ng $(ARCH)

$(CC) $(OBJCOPY) $(SIZE):				$(CTNG)/.config
										$(VECHO) "CROSSTOOL-NG BUILD"
										mkdir -p $(CTNG)/sources
										$(Q) $(MAKE) -C $(CTNG) -f ct-ng build

ctng:									$(CC) $(OBJCOPY) $(SIZE)

ctng-clean:
										git submodule deinit -f $(CTNG)

# lwip

$(LWIP)/README:
						$(VECHO) "LWIP SUBMODULE INIT"
						$(Q) git submodule init $(LWIP)
						$(Q) git submodule update $(LWIP)

$(LWIP_SYSROOT_LIB):	$(LWIP)/README
						$(Q) mkdir -p $(LWIP_SYSROOT_LIB)

$(LWIP_LIB_FILE):		$(LWIP_SYSROOT_LIB) $(LWIP_OBJS)
						$(VECHO) "AR LWIP $@ $<"
						$(Q) rm -f $@ 2> /dev/null
						$(Q) ar r $@ $(LWIP_OBJS)

lwip:					$(LWIP_LIB_FILE)

lwip_clean:
						$(VECHO) "LWIP CLEAN"
						$(Q) git submodule deinit -f $(LWIP)

# lwip Espressif contributions

LWIP_ESPRESSIF_OBJ := $(LWIP_ESPRESSIF)/dhcpserver.o

$(LWIP_ESPRESSIF_LIB_FILE):	$(LWIP_ESPRESSIF_OBJ)
							$(VECHO) "AR ESPRESSIF LWIP $@ $<"
							$(Q) rm -f $@ 2> /dev/null
							ar r $@ $(LWIP_ESPRESSIF_OBJ)

lwip_espressif:				$(LWIP_ESPRESSIF_LIB_FILE)

lwip_espressif_clean:
							$(VECHO) "LWIP ESPRESSIF CLEAN"
							$(Q) rm -f $(LWIP_ESPRESSIF_OBJ) $(LWIP_ESPRESSIF_LIB_FILE)

application.o:			$(HEADERS)
config.o:				$(HEADERS)
display.o:				$(HEADERS)
display_cfa634.o:		$(HEADERS)
display_lcd.o:			$(HEADERS)
display_orbital.o:		$(HEADERS)
display_saa.o:			$(HEADERS)
display_seeed.o:		$(HEADERS)
display_eastrising.o:	$(HEADERS)
display_ssd1306.o:		$(HEADERS)
display_font_6x8.o:		$(HEADERS)
http.o:					$(HEADERS)
i2c.o:					$(HEADERS)
i2c_sensor.o:			$(HEADERS)
i2s.o:					$(HEADERS)
io_aux.o:				$(HEADERS)
io.o:					$(HEADERS)
io_gpio.o:				$(HEADERS)
io_mcp.o:				$(HEADERS)
io_ledpixel.o:			$(HEADERS)
io_pcf.o:				$(HEADERS)
mailbox.o:				$(HEADERS)
queue.o:				queue.h
spi.o:					$(HEADERS)
stats.o:				$(HEADERS) always
time.o:					$(HEADERS)
uart.o:					$(HEADERS)
dispatch.o:				$(HEADERS)
util.o:					$(HEADERS)
sequencer.o:			$(HEADERS)
rboot-interface.o:		$(HEADERS)
lwip-interface.o:		$(HEADERS)
sys_time.o:				$(HEADERS)
sys_string.o:			$(HEADERS)
$(LINKMAP):				$(ELF_IMAGE)

$(ESPTOOL2_BIN):
						$(VECHO) "MAKE ESPTOOL2"
						$(Q) $(MAKE) -C $(ESPTOOL2) $(MAKEMINS) CFLAGS="-O3 -Wall -s -Wno-stringop-truncation"

$(RBOOT_BIN):			$(ESPTOOL2_BIN)
						$(VECHO) "MAKE RBOOT"
						$(Q) $(MAKE) $(MAKEMINS) -C $(RBOOT) CC=$(CC) LD=$(CC) RBOOT_BIG_FLASH=1 RBOOT_RTC_ENABLED=1 SPI_SIZE=$(RBOOT_SPI_SIZE) SPI_MODE=$(SPI_FLASH_MODE)

$(LDSCRIPT):			$(LDSCRIPT_TEMPLATE)
						$(VECHO) "LINKER SCRIPT $(LD_ADDRESS) $(LD_LENGTH) $@"
						$(Q) sed -e 's/@IROM0_SEG_ADDRESS@/$(LD_ADDRESS)/' -e 's/@IROM_SEG_LENGTH@/$(LD_LENGTH)/' < $< > $@

$(LIBMAIN_RBB_FILE):	$(LIBMAIN_ORIGINAL_FILE)
						$(VECHO) "TWEAK LIBMAIN $@"
						$(Q) $(OBJCOPY) -W Cache_Read_Enable_New $< $@

$(ELF_IMAGE):			$(LWIP_LIB_FILE) $(OBJS) $(LWIP_ESPRESSIF_LIB_FILE) $(LIBMAIN_RBB_FILE) $(LDSCRIPT)
						$(VECHO) "LD"
						$(Q) $(CC) -T./$(LDSCRIPT) $(CFLAGS) $(LDFLAGS) $(OBJS) -Wl,--start-group -l$(LIBMAIN_RBB) $(SDKLIBS) $(STDLIBS) $(LWIPLIBS) -Wl,--end-group -o $@

$(FIRMWARE_RBOOT):		$(RBOOT_BIN)
						cp $< $@

$(FIRMWARE_IMG):		$(ELF_IMAGE) $(ESPTOOL2_BIN)
						$(VECHO) "RBOOT FIRMWARE $@"
						$(Q) $(ESPTOOL2_BIN) -quiet -bin -$(FLASH_SIZE_ESPTOOL2) -$(SPI_FLASH_MODE) -boot2 $< $@ .text .data .rodata

$(CONFIG_RBOOT_ELF):	$(CONFIG_RBOOT_SRC)

$(CONFIG_RBOOT_BIN):	$(CONFIG_RBOOT_ELF)
						$(VECHO) "RBOOT CONFIG $@"
						$(Q) $(OBJCOPY) --output-target binary $< $@

flash:					$(ALL_BUILD_TARGETS) $(ALL_IMAGE_TARGETS) $(ALL_EXTRA_TARGETS) $(ALL_TOOL_TARGETS)
						$(VECHO) "FLASH"
						$(Q) $(ESPTOOL) write_flash --flash_size $(FLASH_SIZE_ESPTOOL) --flash_mode $(SPI_FLASH_MODE) \
							$(OFFSET_BOOT) $(FIRMWARE_RBOOT) \
							$(OFFSET_RBOOT_CFG) $(CONFIG_RBOOT_BIN) \
							$(OFFSET_IMG_0) $(FIRMWARE_IMG) \
							$(PHYDATA_OFFSET) $(PHYDATA_FILE) \
							$(RFCAL_OFFSET) $(RFCAL_FILE) \
							$(SYSTEM_CONFIG_OFFSET) $(SYSTEM_CONFIG_FILE)

ota:					$(ALL_BUILD_TARGETS) $(ALL_IMAGE_TARGETS) $(ALL_FLASH_TARGETS) $(ALL_EXTRA_TARGETS)
						$(VECHO) "OTA"
						$(OTA_FLASH) -h $(OTA_HOST) -f $(FIRMWARE_IMG) -W

ota-default:			$(PHYDATA_FILE) $(SYSTEM_CONFIG_FILE) $(RFCAL_FILE)
						$(VECHO) "OTA DEFAULTS"
						$(VECHO) "* rf config"
						$(Q)$(OTA_FLASH) -n -N -h $(OTA_HOST) -f $(PHYDATA_FILE) -s $(PHYDATA_OFFSET) -W
						$(VECHO) "* system_config"
						$(Q)$(OTA_FLASH) -n -N -h $(OTA_HOST) -f $(SYSTEM_CONFIG_FILE) -s $(SYSTEM_CONFIG_OFFSET) -W
						$(VECHO) "* rf calibiration"
						$(Q)$(OTA_FLASH) -n -N -h $(OTA_HOST) -f $(RFCAL_FILE) -s $(RFCAL_OFFSET) -W

ota-rboot-update:		$(FIRMWARE_RBOOT) ota-default $(FIRMWARE_IMG) $(ALL_EXTRA_TARGETS)
						$(VECHO) "FLASH RBOOT"
						$(Q) $(OTA_FLASH) -n -N -h $(OTA_HOST) -f $(FIRMWARE_RBOOT) -s $(OFFSET_BOOT) -W
						$(VECHO) "FLASH"
						$(Q) $(OTA_FLASH) -h $(OTA_HOST) -f $(FIRMWARE_IMG) -W -t

backup-config:
						$(VECHO) "BACKUP CONFIG"
						$(Q) $(ESPTOOL) read_flash $(USER_CONFIG_OFFSET) 0x1000 $(CONFIG_BACKUP_BIN)

restore-config:
						$(VECHO) "RESTORE CONFIG"
						$(Q) $(ESPTOOL) write_flash --flash_size $(FLASH_SIZE_ESPTOOL) --flash_mode $(SPI_FLASH_MODE) \
							$(USER_CONFIG_OFFSET) $(CONFIG_BACKUP_BIN)

wipe-config:
						$(VECHO) "WIPE CONFIG"
						dd if=/dev/zero of=wipe-config.bin bs=4096 count=1
						$(Q) $(ESPTOOL) write_flash --flash_size $(FLASH_SIZE_ESPTOOL) --flash_mode $(SPI_FLASH_MODE) \
							$(USER_CONFIG_OFFSET) wipe-config.bin
						rm wipe-config.bin

%.o:					%.c
						$(VECHO) "CC $<"
						$(Q) $(CC) $(CCWARNINGS) $(CFLAGS) $(CINC) -c $< -o $@

%.i:					%.c
						$(VECHO) "CC cpp $<"
						$(Q) $(CC) -E $(CCWARNINGS) $(CFLAGS) $(CINC) -c $< -o $@

%.s:					%.c
						$(VECHO) "CC as $<"
						$(Q) $(CC) -S $(CCWARNINGS) $(CFLAGS) $(CINC) -c $< -o $@

%:						%.cpp
						$(VECHO) "HOST CPP $<"
						$(Q) $(HOSTCPP) $(HOSTCPPFLAGS) $< -o $@

%.h.gch:				%.h
						$(VECHO) "HOST CPP PCH $<"
						$(Q) $(HOSTCPP) $(HOSTCPPFLAGS) -c -x c++-header $< -o $@

espif:					espif.cpp espif.h.gch
espflash:				espflash.cpp
resetserial:			resetserial.cpp


rxtest:
						$(OTA_FLASH) --read --host $(OTA_HOST) --file test --length 390352 --start 0x002000

txtest:
						$(OTA_FLASH) --simulate --host $(OTA_HOST) --file $(FIRMWARE_IMG)

benchmark:
						$(OTA_FLASH) --benchmark --host $(OTA_HOST)

wvtest:
						$(OTA_FLASH) --write   --host $(OTA_HOST) --start $(PICTURE_FLASH_OFFSET_0) --file testpicture.ppm
						$(OTA_FLASH) --verify  --host $(OTA_HOST) --start $(PICTURE_FLASH_OFFSET_0) --file testpicture.ppm

vtest:
						$(OTA_FLASH) --verify  --host $(OTA_HOST) --start $(PICTURE_FLASH_OFFSET_0) --file testpicture.ppm

test:					rxtest txtest benchmark

section_free	= $(Q) perl -e '\
						open($$fd, "$(SIZE) -A $(1) |"); \
						$$available = $(6); \
						$$used = 0; \
						while(<$$fd>) \
						{ \
							chomp; \
							@_ = split; \
							if(($$_[0] eq "$(3)") || ($$_[0] eq "$(4)") || ($$_[0] eq "$(5)")) \
							{ \
								$$used += $$_[1]; \
							} \
						} \
						$$free = $$available - $$used; \
						printf("    %-8s available: %3u k, used: %6u, free: %6u, %2u %%\n", "$(2)" . ":", $$available / 1024, $$used, $$free, 100 * $$free / $$available); \
						close($$fd);'
