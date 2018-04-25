#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

$GnumericTest::default_corpus = 'random:5';

&message ("Check the xlsx importer/exporter with valgrind.");

my $unzip = &GnumericTest::find_program ("unzip");

my $format = "Gnumeric_Excel:xlsx";

my @sources = &GnumericTest::corpus ();
my $nskipped = 0;
my $ngood = 0;
my $nbad = 0;

foreach my $src (@sources) {
    if (!-r $src) {
	$nskipped++;
	next;
    }

    my $basename = &File::Basename::fileparse ($src);
    my $basenoext = $basename;
    $basenoext =~ s/\.[^.]+$//;

    my $tmp1 = "$basenoext-tmp.xlsx";
    print STDERR "$basename -> $tmp1\n";
    &GnumericTest::junkfile ($tmp1);
    my $cmd = &GnumericTest::quotearg ($ssconvert, "-T", $format, $src, $tmp1);
    my $err = &test_valgrind ($cmd, 1, 1);

    if (!$err && $basename !~ /\.xlsx$/) {
	my $tmp2 = "$basenoext-tmp.gnumeric";
	print STDERR "$tmp1 -> $tmp2\n";
	&GnumericTest::junkfile ($tmp2);

	my $cmd = &GnumericTest::quotearg ($ssconvert, $tmp1, $tmp2);
	$err = &test_valgrind ($cmd, 1, 1);
	&GnumericTest::removejunk ($tmp2);
    }

    $err ? $nbad++ : $ngood++;
    &GnumericTest::removejunk ($tmp1);
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

# -----------------------------------------------------------------------------
