# generate bitmap fonts for graphical displays

HOST =				esp1
BIN =				fonts.bin
BDF2C =				bdf2c
FONT_5x8 =			spleen-5x8.bdf
FONT_6x12 =			ter-u12b.bdf
FONT_8x14 =			ter-u14b.bdf
FONT_10x18 =		ter-u18b.bdf
FONT_12x24 =		ter-u24b.bdf
FONT_14x28 =		ter-u28b.bdf
FONT_16x32 =		ter-u32b.bdf
FONTS =				$(FONT_5x8) $(FONT_6x12) $(FONT_8x14) $(FONT_10x18) $(FONT_12x24) $(FONT_14x28) $(FONT_16x32)
FONT_CODE_RANGE =	32-126,160-165,169,171,173-174,176,181,187,191,223-252,913-939,945-981,8211,8212,8224-8226,8364,8482,8486,9834,9835

MAKEFLAGS += --no-builtin-rules

.PRECIOUS:				bdf2c.c Makefile
.PHONY:					always

all:					$(BIN)

$(BIN):					$(BDF2C) $(FONTS)
						$(BDF2C) $(BIN) $(FONT_CODE_RANGE) \
								$(FONT_5x8)		 5  8 8 3 0 0 \
								$(FONT_6x12)	 6 12 8 2 0 0 \
								$(FONT_8x14)	 8 14 8 1 0 0 \
								$(FONT_10x18)	10 18 0 6 0 0 \
								$(FONT_12x24)	12 24 0 4 1 0 \
								$(FONT_14x28)	14 28 0 2 1 0 \
								$(FONT_16x32)	16 32 0 0 2 0 \

flash:					$(BIN)
						@if [ `stat --format=%s $(BIN)` -gt 196608 ]; then echo "*** font image too large ***"; exit 1; fi
						../espif --host $(HOST) --filename $(BIN) --start 0x0c6000 --write
						../espif --host $(HOST) --filename $(BIN) --start 0x1c6000 --write
						../espif --host $(HOST) --filename $(BIN) --start 0x0c6000 --verify
						../espif --host $(HOST) --filename $(BIN) --start 0x1c6000 --verify

$(BDF2C):				$(BDF2C).c
						gcc -Wall -Werror -O3 -s $< -o $@

clean:
						rm -f $(BDF2C) $(BIN)
