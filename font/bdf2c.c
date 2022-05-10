#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>

enum
{
	fonts = 16,
	ranges = 64,
};

bool process_file(const char *input, FILE *output, unsigned int x_dim, unsigned int y_dim,
		unsigned int shave_left, unsigned int shave_right, unsigned int shave_top, unsigned int shave_bottom,
		const unsigned int range[ranges][2], unsigned int cooked_dimensions[2], unsigned int *glyphs)
{
	FILE *fontfile;
	char linebuf[1024], sarg1[1024], sarg2[1024];
	int iarg1, iarg2, iarg3, iarg4;
	bool bitmap_state;
	char current_char_name[1024];
	unsigned int current_char_codepoint = 0;
	unsigned int hexval, hexvaladd, binval, binbitindex;
	const uint8_t *lptr;
	int byteindex, bitindex;
	unsigned int width, height;
	const uint8_t preamble[8] = {
			0xfc, 0x4a, 0xfb, 0x4a,
			0x01, 0x00, 0x00, 0x00 }; // font_t
	uint8_t bin_buffer[4];
	unsigned int range_index;
	bool include;

	if((fontfile = fopen(input, "r")) == (FILE *)0)
	{
		perror("input cannot be opened");
		return(false);
	}

	if(fwrite(preamble, sizeof(preamble), 1, output) != 1)
	{
		perror("write to output(1)\n");
		return(false);
	}

	width =	16 - shave_left - shave_right;
	height = y_dim - shave_top - shave_bottom;

	cooked_dimensions[0] = width;
	cooked_dimensions[1] = height;

	printf("*** font %s dimensions: %u[16-%u-%u] x %u[-%u-%u] -> %u x %u\n", input,
			x_dim, shave_left, shave_right,
			y_dim, shave_top, shave_bottom,
			width, height);

	for(range_index = 0; (range_index < ranges) && (range[range_index][1] > 0); range_index++)
		printf("*** range[%u]: from %u to %u\n", range_index, range[range_index][0], range[range_index][1]);

	bin_buffer[0] = (width & 0x000000ff) >> 0;
	bin_buffer[1] = (width & 0x0000ff00) >> 8;
	bin_buffer[2] = (width & 0x00ff0000) >> 16;
	bin_buffer[3] = (width & 0xff000000) >> 24;

	if(fwrite(bin_buffer, sizeof(bin_buffer), 1, output) != 1)
	{
		perror("write to output(2)\n");
		return(false);
	}

	bin_buffer[0] = (height & 0x000000ff) >> 0;
	bin_buffer[1] = (height & 0x0000ff00) >> 8;
	bin_buffer[2] = (height & 0x00ff0000) >> 16;
	bin_buffer[3] = (height & 0xff000000) >> 24;

	if(fwrite(bin_buffer, sizeof(bin_buffer), 1, output) != 1)
	{
		perror("write to output(2)\n");
		return(false);
	}

	bitmap_state = false;

	while(fgets(linebuf, sizeof(linebuf), fontfile))
	{
		if(!bitmap_state)
		{
			if(!strcmp(linebuf, "BITMAP\n"))
			{
				byteindex = 0;
				bitmap_state = true;

				include = false;

				for(range_index = 0; (range_index < ranges) && (range[range_index][1] > 0); range_index++)
					if((range[range_index][0] <= current_char_codepoint) && (range[range_index][1] >= current_char_codepoint))
					{
						include = true;
						break;
					}

				if(!include)
					continue;

				printf("\ncode: %u \"%s\"\n", current_char_codepoint, current_char_name);

				bin_buffer[0] = (current_char_codepoint & 0x000000ff) >> 0;
				bin_buffer[1] = (current_char_codepoint & 0x0000ff00) >> 8;
				bin_buffer[2] = (current_char_codepoint & 0x00ff0000) >> 16;
				bin_buffer[3] = (current_char_codepoint & 0xff000000) >> 24;
				bin_buffer[3] |= 1 << 7;

				if(fwrite(bin_buffer, sizeof(bin_buffer), 1, output) != 1)
				{
					perror("write output file (3)");
					exit(-1);
				}

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
					if((x_dim != iarg1) || (y_dim != iarg2))
					{
						fprintf(stderr, "dimensions not constant\n");
						return(false);
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
						return(false);
					}

					continue;
				}

				if(!strcmp("STARTCHAR", sarg1))
				{
					unsigned int len;

					strncpy(current_char_name, linebuf + 10, sizeof(current_char_name));
					len = strlen(current_char_name);

					if(current_char_name[len - 1] == '\n')
						current_char_name[len - 1] = '\0';

					continue;
				}
			}
		}
		else
		{
			if(!strcmp("ENDCHAR\n", linebuf))
			{
				bitmap_state = false;
				(*glyphs)++;
				continue;
			}

			if(!include)
				continue;

			for(lptr = (const uint8_t *)linebuf, hexval = 0; *lptr != '\0'; lptr++)
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

				hexval <<= 4;
				hexval |= hexvaladd;
			}

			if((byteindex >= shave_top) && (byteindex < (y_dim - shave_bottom)))
			{
				printf("%2u: ", byteindex);

				for(bitindex = 15 - shave_left, binval = 0, binbitindex = 0; bitindex >= (int)shave_right; bitindex--, binbitindex++)
				{
					printf("%c", (hexval & (1 << bitindex)) ? 'X' : '.');

					if(hexval & (1 << bitindex))
						binval |= 1 << binbitindex;
				}

				bin_buffer[0] = (binval & 0x000000ff) >> 0;
				bin_buffer[1] = (binval & 0x0000ff00) >> 8;
				bin_buffer[2] = (binval & 0x00ff0000) >> 16;
				bin_buffer[3] = (binval & 0xff000000) >> 24;

				if(fwrite(bin_buffer, sizeof(bin_buffer), 1, output) != 1)
				{
					perror("write output file (4)");
					exit(-1);
				}

				printf(" %04x %02x%02x%02x%02x\n", hexval, bin_buffer[0], bin_buffer[1], bin_buffer[2], bin_buffer[3]);
			}

			byteindex++;
		}
	}

	bin_buffer[0] = 0xff;
	bin_buffer[1] = 0xff;
	bin_buffer[2] = 0xff;
	bin_buffer[3] = 0xff;

	if(fwrite(bin_buffer, sizeof(bin_buffer), 1, output) != 1)
	{
		perror("write output file (5)");
		exit(-1);
	}

	printf("*** font finished, %u glyphs\n", *glyphs);

	return(true);
}

typedef enum
{
	s_start,
	s_from,
	s_minus,
	s_to,
	s_comma,
} state_t;

int main(int argc, char *const argv[])
{
	FILE *output;
	unsigned int ix, curarg;
	unsigned int x_dim, y_dim;
	unsigned int shave_left, shave_right;
	unsigned int shave_top, shave_bottom;
	const char *input;
	const uint8_t preamble[72] = {
			0xfb, 0x4a, 0xfb, 0x4a,
			0x01, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00,	// #0
			0x00, 0x00, 0x00, 0x00,	// #1
			0x00, 0x00, 0x00, 0x00,	// #2
			0x00, 0x00, 0x00, 0x00,	// #3
			0x00, 0x00, 0x00, 0x00,	// #4
			0x00, 0x00, 0x00, 0x00,	// #5
			0x00, 0x00, 0x00, 0x00,	// #6
			0x00, 0x00, 0x00, 0x00,	// #7
			0x00, 0x00, 0x00, 0x00,	// #8
			0x00, 0x00, 0x00, 0x00,	// #9
			0x00, 0x00, 0x00, 0x00,	// #10
			0x00, 0x00, 0x00, 0x00,	// #11
			0x00, 0x00, 0x00, 0x00,	// #12
			0x00, 0x00, 0x00, 0x00,	// #13
			0x00, 0x00, 0x00, 0x00,	// #14
			0x00, 0x00, 0x00, 0x00, // #15
	}; // font_root_t
	uint8_t bin_offset[fonts * 4];
	uint32_t offset[fonts];
	unsigned int next_offset, file_length;
	const char *fontfile[fonts];
	const char *range_string;
	unsigned int raw_dimensions[fonts][2];
	unsigned int cooked_dimensions[fonts][2];
	unsigned int glyphs[fonts];
	unsigned int range[ranges][2];
	state_t state;

	if(argc < 2)
	{
		fprintf(stderr, "usage: bdf2c <output_file> <included codes> <<font_file> <x_dim> <y_dim> <shave_left> <shave_right> <shave_top> <shave_bottom>>\n");
		return(-1);
	}

	if((output = fopen(argv[1], "w")) == (FILE *)0)
	{
		perror("output file cannot be opened");
		exit(-1);
	}

	for(ix = 0; ix < fonts; ix++)
	{
		offset[ix] = 0;
		fontfile[ix] = "";
		raw_dimensions[ix][0] = raw_dimensions[ix][1] = 0;
		cooked_dimensions[ix][0] = cooked_dimensions[ix][1] = 0;
		glyphs[ix] = 0;
	}

	for(ix = 0; ix < ranges; ix++)
		range[ix][0] = range[ix][1] = 0;

	if(fwrite(preamble, sizeof(preamble), 1, output) != 1)
	{
		perror("write output file (1)");
		exit(-1);
	}

	range_string = argv[2];

	unsigned int current_char;
	unsigned int current_range = 0;
	unsigned int current_value_from = 0;
	unsigned int current_value_to = 0;
	state = s_start;

	for(current_char = 0; (current_char < strlen(range_string)) && (current_range < ranges); current_char++)
	{
		switch(state)
		{
			case(s_start):
			{
				if((range_string[current_char] >= '0') && (range_string[current_char] <= '9'))
				{
					current_value_from = range_string[current_char] - '0';
					state = s_from;
					continue;
				}

				fprintf(stderr, "range: invalid syntax (1)\n");
				exit(1);
			}

			case(s_from):
			{
				if((range_string[current_char] >= '0') && (range_string[current_char] <= '9'))
				{
					current_value_from = (current_value_from * 10) + (range_string[current_char] - '0');
					continue;
				}

				if(range_string[current_char] == '-')
				{
					state = s_minus;
					continue;
				}

				if(range_string[current_char] == ',')
				{
					range[current_range][0] = current_value_from;
					range[current_range][1] = current_value_from;
					current_value_from = current_value_to = 0;
					current_range++;

					state = s_start;
					continue;
				}

				fprintf(stderr, "range: invalid syntax (2)\n");
				exit(1);
			}

			case(s_minus):
			{
				if((range_string[current_char] >= '0') && (range_string[current_char] <= '9'))
				{
					current_value_to = range_string[current_char] - '0';
					state = s_to;
					continue;
				}

				fprintf(stderr, "range: invalid syntax (3)\n");
				exit(1);
			}

			case(s_to):
			{
				if((range_string[current_char] >= '0') && (range_string[current_char] <= '9'))
				{
					current_value_to = (current_value_to * 10) + (range_string[current_char] - '0');
					continue;
				}

				if(range_string[current_char] == ',')
				{
					range[current_range][0] = current_value_from;
					range[current_range][1] = current_value_to;
					current_value_from = current_value_to = 0;
					current_range++;

					state = s_start;

					continue;
				}

				fprintf(stderr, "range: invalid syntax (2)\n");
				exit(1);
			}

			default:
			{
				fprintf(stderr, "range: internal error\n");
				exit(1);
			}
		}
	}

	if((current_range < ranges) && (current_value_from > 0))
	{
		if(current_value_to == 0)
			current_value_to = current_value_from;

		range[current_range][0] = current_value_from;
		range[current_range][1] = current_value_to;
		current_value_from = current_value_to = 0;
		current_range++;
	}

	for(ix = 0, curarg = 3; ix < fonts; ix++, curarg += 7)
	{
		if(curarg == argc)
			break;

		if((curarg + 6) > argc)
		{
			fprintf(stderr, "missing arguments\n");
			exit(1);
		}

		input = argv[curarg + 0];
		fontfile[ix] = input;
		x_dim = strtoul(argv[curarg + 1], (char **)0, 0);
		y_dim = strtoul(argv[curarg + 2], (char **)0, 0);
		raw_dimensions[ix][0] = x_dim;
		raw_dimensions[ix][1] = y_dim;
		shave_left = strtoul(argv[curarg + 3], (char **)0, 0);
		shave_right = strtoul(argv[curarg + 4], (char **)0, 0);
		shave_top = strtoul(argv[curarg + 5], (char **)0, 0);
		shave_bottom = strtoul(argv[curarg + 6], (char **)0, 0);

		offset[ix] = ftell(output);

		if(!process_file(input, output, x_dim, y_dim, shave_left, shave_right, shave_top, shave_bottom, range, cooked_dimensions[ix], &glyphs[ix]))
			break;
	}

	file_length = ftell(output);

	if(fseek(output, 8, SEEK_SET))
	{
		perror("fseek");
		exit(-1);
	}

	for(unsigned int font = 0; font < fonts; font++)
	{
		if((font + 1) < fonts)
			next_offset = offset[font + 1];
		else
			next_offset = 0;

		if(next_offset == 0)
		{
			if(offset[font] == 0)
				next_offset = 0;
			else
				next_offset = file_length;
		}

		printf("%2u %-20s raw dimensions: %2u x %2u, cooked dimensions: %2u x %2u, offset: %6u, glyphs: %4u, length: %u\n", font, fontfile[font],
				raw_dimensions[font][0], raw_dimensions[font][1],
				cooked_dimensions[font][0], cooked_dimensions[font][1],
				offset[font], glyphs[font], next_offset - offset[font]);

		for(unsigned int byte = 0; byte < 4; byte++)
			bin_offset[(font * 4) + byte] = (offset[font] & (0xff << (byte << 3))) >> (byte << 3);
	}

	if(fwrite(bin_offset, sizeof(bin_offset), 1, output) != 1)
	{
		perror("write output file (2)");
		exit(-1);
	}

	printf("- file length: %u bytes, %u kbytes, %u sectors\n", file_length, file_length / 1024, file_length / 4096);

	fclose(output);
}
