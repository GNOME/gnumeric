#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check that the gnumeric exporter produces the same results every time.");

my $format = "Gnumeric_XmlIO:sax:0";

my @sources = &GnumericTest::corpus();
# datefuns and docs-samples use NOW()
@sources = grep { !m{(^|/)(datefuns\.xls|docs-samples\.gnumeric)$} } @sources;

my $nskipped = 0;
my $ngood = 0;
my $nbad = 0;

foreach my $src (@sources) {
    if (!-r $src) {
	$nskipped++;
	next;
    }

    print STDERR "Checking $src\n";

    my @data;
    foreach my $i (1, 2) {
	my $tmp = $src;
	$tmp =~ s|^.*/||;
	$tmp =~ s|\..*|-$i.gnumeric|;
	&GnumericTest::junkfile ($tmp);
	my $cmd = "$ssconvert -T $format $src $tmp";
	print STDERR "# $cmd\n" if $GnumericTest::verbose;
	system ($cmd);
	if (!-r $tmp) {
	    print STDERR "ssconvert failed to produce $tmp\n";
	    die "Fail\n";
	}

	my $d = &GnumericTest::read_file ($tmp);

	# Some formats (notably mps) set this to current time.
	$d =~ s{<meta:creation-date>[0-9-:TZ]+</meta:creation-date>}{};

	push @data, $d;
	&GnumericTest::removejunk ($tmp);
    }

    if ($data[0] ne $data[1]) {
	print STDERR "Generated output for $src is not deterministic.\n";
	$nbad++;
    } else {
	$ngood++;
    }
}

&GnumericTest::report_skip ("No source files present") if $nbad + $ngood == 0;

if ($nskipped > 0) {
    print STDERR "$nskipped files skipped.\n";
}

if ($nbad > 0) {
    die "Fail\n";
} else {
    print STDERR "Pass\n";
}
