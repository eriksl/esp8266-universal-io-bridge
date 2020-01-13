#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>

int main(int argc, char *const argv[])
{
	FILE *fontfile, *hfile, *cfile;
	char linebuf[1024], sarg1[1024], sarg2[1024];
	int iarg1, iarg2, iarg3, iarg4;
	bool bitmap_state;
	char current_char_name[1024];
	unsigned int current_char_codepoint = 0;
	unsigned int hexval[2], hexvaladd;
	unsigned int evenodd;
	const uint8_t *lptr;

	if(argc != 4)
	{
		fprintf(stderr, "usage: bdf2c fonf_file.bdf h_file.h c_file.c\n");
		return(-1);
	}

	if((fontfile = fopen(argv[1], "r")) == (FILE *)0)
	{
		perror("input cannot be opened");
		return(-1);
	}

	if((hfile = fopen(argv[2], "w")) == (FILE *)0)
	{
		perror("output h_file cannot be opened");
		return(-1);
	}

	if((cfile = fopen(argv[3], "w")) == (FILE *)0)
	{
		perror("output c_file cannot be opened");
		return(-1);
	}

	fputs(""
			"#ifndef _font_bitmap_h_\n"
			"#define _font_bitmap_h_\n"
			"\n"
			"#define assert_size(type, size) _Static_assert(sizeof(type) == size, \"sizeof(\" #type \") != \" #size)\n"
			"\n"
			"#include <stdint.h>\n"
			"\n"
			"enum\n"
			"{\n"
			"	font_magic = 0x4afb4afb,\n"
			"	font_version = 0,\n"
			"	font_codepoint_last_entry = ~0,\n"
			"};\n"
			"\n"
			"typedef struct\n"
			"{\n"
				"	uint32_t codepoint;\n"
				"	uint32_t bitmap[16]; // each entry is 16 bits of the even line and 16 bits of the following odd line\n"
			"} font_bitmap_entry_t;\n"
			"\n"
			"assert_size(font_bitmap_entry_t, 68);\n"
			"\n"
			"typedef struct\n"
			"{\n"
				"	uint32_t			magic;\n"
				"	uint32_t			version;\n"
				"	font_bitmap_entry_t	entries[];\n"
			"} font_bitmap_t;\n"
			"\n"
			"#endif /*_font_bitmap_h_*/\n",
		hfile);

	fprintf(cfile, "#include \"%s\"\n\n", argv[2]);

	fputs(""
			"const font_bitmap_t font_bitmap = \n"
			"{\n"
			"	font_magic, font_version,\n"
			"	{\n",
		cfile);

	bitmap_state = false;

	while(fgets(linebuf, sizeof(linebuf), fontfile))
	{
		if(!bitmap_state)
		{
			if(!strcmp(linebuf, "BITMAP\n"))
			{
				bitmap_state = true;
				evenodd = 0;

				fprintf(cfile, "		{\n");
				fprintf(cfile, "			%u,\n", current_char_codepoint);
				fprintf(cfile, "			{\n");

				continue;
			}

			if(sscanf(linebuf, "%1024s %d", sarg1, &iarg1) == 2)
			{
				if(!strcmp("ENCODING", sarg1))
				{
					current_char_codepoint = (unsigned int)iarg1;

					continue;
				}
			}

			if(sscanf(linebuf, "%1024s %d %d %d %d", sarg1, &iarg1, &iarg2, &iarg3, &iarg4) == 5)
			{
				if(!strcmp("FONTBOUNDINGBOX", sarg1))
				{
					if(iarg1 != 16)
					{
						fprintf(stderr, "font width != 16\n");
						exit(-1);
					}

					if(iarg2 != 32)
					{
						fprintf(stderr, "font height != 32\n");
						exit(-1);
					}

					continue;
				}
			}

			if(sscanf(linebuf, "%1024s %1024s", sarg1, sarg2) == 2)
			{
				if(!strcmp("CHARSET_REGISTRY", sarg1))
				{
					if(strcmp("\"ISO10646\"", sarg2))
					{
						fprintf(stderr, "encoding should be ISO 10646\n");
						exit(-1);
					}

					continue;
				}

				if(!strcmp("STARTCHAR", sarg1))
				{
					strncpy(current_char_name, sarg2, sizeof(current_char_name));

					continue;
				}
			}
		}
		else
		{
			if(!strcmp("ENDCHAR\n", linebuf))
			{
				fprintf(cfile, "			},\n");
				fprintf(cfile, "		},\n");
				bitmap_state = false;
				continue;
			}

			for(lptr = (const uint8_t *)linebuf, hexval[evenodd] = 0; *lptr != '\0'; lptr++)
			{
				hexvaladd = 0;

				if((*lptr >= '0') && (*lptr <= '9'))
					hexvaladd = *lptr - '0';
				else
					if((*lptr >= 'a') && (*lptr <= 'f'))
						hexvaladd = (*lptr - 'a') + 10;
					else
						if((*lptr >= 'A') && (*lptr <= 'F'))
							hexvaladd = (*lptr - 'A') + 10;
						else
							break;

				hexval[evenodd] <<= 4;
				hexval[evenodd] |= hexvaladd;
			}

			unsigned int bitindex;

			if(evenodd)
				fprintf(cfile, "				0x%04x%04x, // ", hexval[0], hexval[1]);
			else
				fprintf(cfile, "		            		// ");

			for(bitindex = 15; bitindex > 0; bitindex--)
				fprintf(cfile, "%c ", (hexval[evenodd] & (1 << bitindex)) ? 'X' : '.');

			fprintf(cfile, "\n");

			evenodd ^= 0x01;
		}
	}

	fputs(""
			"		{\n"
			"			font_codepoint_last_entry,\n"
			"			{\n"
			"				0x00000000,\n"
			"				0x00000000,\n"
			"				0x00000000,\n"
			"				0x00000000,\n"
			"				0x00000000,\n"
			"				0x00000000,\n"
			"				0x00000000,\n"
			"				0x00000000,\n"
			"				0x00000000,\n"
			"				0x00000000,\n"
			"				0x00000000,\n"
			"				0x00000000,\n"
			"				0x00000000,\n"
			"				0x00000000,\n"
			"				0x00000000,\n"
			"				0x00000000,\n"
			"			},\n"
			"		},\n"
			"	},\n",
		cfile);

	fprintf(cfile, "};\n");

	fclose(hfile);
	fclose(cfile);

	return(0);
}
