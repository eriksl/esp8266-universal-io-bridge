# generate bitmap 32x16 font for Eastrising graphical LCD

HOST =					esp1
FONT =					font_32x16
BDF2C =					bdf2c

MAKEFLAGS += --no-builtin-rules

.PRECIOUS:				bdf2c.c Makefile $(FONT).bdf

all:					$(FONT).bin

flash:					$(FONT).bin
						../espflash --filename $(FONT).bin --host $(HOST) --start 0x0d6000 --write
						../espflash --filename $(FONT).bin --host $(HOST) --start 0x1d6000 --write
						../espflash --filename $(FONT).bin --host $(HOST) --start 0x0d6000 --verify
						../espflash --filename $(FONT).bin --host $(HOST) --start 0x1d6000 --verify

clean:
						rm -f $(FONT).bin $(FONT).o $(FONT).c $(FONT).h $(BDF2C)

$(FONT).bin:			$(FONT).o
						objcopy --dump-section .rodata=$(FONT).bin $(FONT).o

$(FONT).o:				$(FONT).c $(FONT).h
						gcc -c -O3 -s $< -o $@


$(FONT).c $(FONT).h:	$(FONT).bdf	$(BDF2C)
						$(BDF2C) $(FONT).bdf $(FONT).h $(FONT).c

$(BDF2C):				$(BDF2C).c
						gcc -O3 -s $< -o $@
