#ifndef esp_alt_register_h
#define esp_alt_register_h

enum
{
	ETS_CCOMPARE0_INUM =	6,
	ETS_SOFT_INUM =			7,
	ETS_WDT_INUM =			8,
};

enum
{
	WDT_CNTL =			0x60000900,
	WDT_STAGE0_RELOAD =	0x60000904,
	WDT_STAGE1_RELOAD =	0x60000908,
	WDT_COUNTER =		0x6000090c,
	WDT_STAGE =			0x60000910,
	WDT_RESET =			0x60000914,
	WDT_RESET_STAGE =	0x60000918,
};

#endif
