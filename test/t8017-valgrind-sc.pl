#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check the sc with valgrind.");

my @sources = ("$samples/sc/demo_func", "$samples/sc/demo_math");
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

    my $tmp1 = &GnumericTest::invent_junkfile ("$basenoext-tmp.gnumeric");
    print STDERR "$basename -> $tmp1\n";
    my $cmd = &GnumericTest::quotearg ($ssconvert, $src, $tmp1);
    my $err = &test_valgrind ($cmd, 1, 1);

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
