#!/usr/bin/perl -w

no warnings 'portable';

use Data::Dumper;

my($input) = $ARGV[0];
my($fd);
my($symbol, $address, $class, $type, $size, $line, $section, $source);
my(%symbols, %value);

my(%sections) = (
	"bss" =>
	{
		"data" => 1,
		"start" => hex("3ffe8000"),
		"end" => hex("3fffbfff"),
	},
	"data" =>
	{
		"data" => 1,
		"start" => hex("3ffe8000"),
		"end" => hex("3fffbfff"),
	},
	"irom0.text" =>
	{
		"data" => 0,
		"start" => hex("0x40200000"),
		"end" => hex("0x402fa000"),
	},
	"rodata" =>
	{
		"data" => 1,
		"start" => hex("0x3ffe8000"),
		"end" => hex("0x3fffbfff"),
	},
	"text" =>
	{
		"data" => 0,
		"start" => hex("0x40100000"),
		"end" => hex("0x40108000"),
	},
);

my(%class_to_text) = (
	"A" => "global absolute",
	"a" => "local absolute",
	"B" => "global bss",
	"b" => "local bss",
	"C" => "common",
	"D" => "global data",
	"d" => "local data",
	"G" => "global sdata",
	"g" => "local sdata",
	"I" => "indirect",
	"N" => "debug",
	"n" => "comment",
	"p" => "stack unwind",
	"R" => "global rodata",
	"r" => "local rodata",
	"S" => "global sbbs",
	"s" => "local sbbs",
	"T" => "global text",
	"t" => "local text",
	"U" => "undefined",
	"u" => "unique",
	"V" => "WEAK1",
	"v" => "weak1",
	"W" => "WEAK2",
	"w" => "weak2",
	"-" => "stab",
	"?" => "unknown",
	"" => "",
);

die("failed to start nm") if(!open($fd, "nm -n -l -a -fsysv --synthetic " . $input . " |"));

while(<$fd>)
{
	chomp();

	($symbol, $address, $class, $type, $size, $line, $section, $source) =
		m/^([^\s|]+)\s*\|\s*([0-9a-f]+)\s*\|\s*([^\s]+)\s*\|\s*([^\s|]*)\s*\|\s*([0-9a-f]*)\s*\|\s*([\^s|]*)\s*\|\s*([a-zA-Z0-9._*]*)\s*(.*)$/o;

	next if(!defined($symbol) || !defined($address) || !defined($class) || !defined($type) || !defined($size) || !defined($line) || !defined($section) || !defined($source));
	next if($section !~ m/^\.[a-zA-Z]+/g);

	$section =~ s/^.//o;

	undef(%value);
	$value{name} = $symbol;
	$value{address} = hex($address);
	$value{size_1} = hex($size);
	$value{class} = $class;
	$value{type} = $type;
	($value{source_file}, $value{source_line}) = $source =~ m/\/([^\/]+):([0-9]+)$/o;

	%{$symbols{section}{$section}{by_name}{$symbol}} = %value;
	%{$symbols{section}{$section}{by_address}{hex($address)}} = %value;
}

close($fd);

my($sections_entry, $previous_address, $this_address, $previous, $this, $key);

for $section (sort(keys(%{$symbols{section}})))
{
	undef($previous_address);

	if(!exists($sections{$section}))
	{
		printf("section %s is unknown\n", $section);
		next;
	}

	$sections_entry = \%{$sections{$section}};
	$$sections_entry{size_1} = 0;
	$$sections_entry{size_2} = 0;

	for $this_address (sort(keys(%{$symbols{section}{$section}{by_address}})))
	{
		if(!defined($previous_address))
		{
			$previous_address = $this_address;
			next;
		}

		$previous =	\%{$symbols{section}{$section}{by_address}{$previous_address}};
		$this =		\%{$symbols{section}{$section}{by_address}{$this_address}};

		if(($$this{address} < $$sections_entry{start}) || ($$this{address} > $$sections_entry{end}))
		{
			printf("   invalid address for symbol %s for section %s: %x\n", $$this{name}, $section, $$this{address});
			next;
		}

		$$previous{size_2} = $$this{address} - $$previous{address};
		$$sections_entry{size_1} += $$previous{size_1};
		$$sections_entry{size_2} += $$previous{size_2};
		$key = sprintf("%-10s:%4x:%s", $section, $$previous{size_2}, $$previous{address});
		%{$symbols{section}{$section}{by_size}{$key}} = %{$previous};

		$previous_address = $this_address;
	}

	$$this{size_2} = $$this{size_1};
	$$sections_entry{size_1} += $$this{size_1};
	$$sections_entry{size_2} += $$this{size_1};

	$key = sprintf("%-10s:%4x:%s", $section, $$this{size_2}, $$this{address});
	%{$symbols{section}{$section}{by_size}{$key}} = %{$this};
}

my(%section_to_region) =
(
	"bss" => "dram",
	"data" => "dram",
	"rodata" => "dram",
	"text" => "iram",
	"irom0.text" => "flash",
);

my($region, %region);

printf("sections\n");

for $section (qw[bss data rodata text irom0.text])
{
	$region = $section_to_region{$section};

	printf(" %-10s used: %6d region: %s\n",
			$section,
			$sections{$section}{size_2},
			$region);

	$region{$region}{name} = $region;
	$region{$region}{start} = $sections{$section}{start};
	$region{$region}{end} = $sections{$section}{end};
	$region{$region}{size} += $sections{$section}{size_2};
}

printf("\nregions\n");

my($length);

for $region (qw[dram iram flash])
{
	$length = $region{$region}{end} - $region{$region}{start};
	$free = $length - $region{$region}{size};

	printf(" %-10s start: 0x%8x end: 0x%8x length: %7d used: %6d free: %6d %3d%%\n",
			$region,
			$region{$region}{start},
			$region{$region}{end},
			$length,
			$region{$region}{size},
			$free,
			($free * 100) / $length,
		);
}

printf("\nsymbols\n");

my($name, $section2, $source_line, $source_file);

printf(" %-10s %-6s %-14s %-8s %-5s %-5s %-44s %-4s %s\n", "section", "region", "class", "address", "size1", "size2", "name", "line", "file");

for $section (qw[bss data rodata text irom0.text])
{
	$region = $section_to_region{$section};

	for $key (sort(keys(%{$symbols{section}{$section}{by_size}})))
	{
		($section2, $size, $address) = $key =~ m/^([^\s]+)\s*:\s*([0-9a-f]+)\s*:(.*)/o;

		$this = \%{$symbols{section}{$section}{by_address}{$address}};

		$source_line = $$this{source_line};
		$source_file = $$this{source_file};

		$source_file = "" if(!defined($source_file));

		if(!defined($source_line))
		{
			$source_line = "";
		}
		else
		{
			$source_line = sprintf("%4s", $source_line);
		}

		printf(" %-10s %-6s %-14s %08x %5d %5d %-44s %-4s %s\n", $section2, $region, $class_to_text{$$this{class}}, $address, $$this{size_1}, $$this{size_2},
				$$this{name}, $source_line, $source_file);
	}
}
