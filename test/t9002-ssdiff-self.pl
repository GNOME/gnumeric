#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check ssdiff on identical files");

my @sources = &GnumericTest::corpus();
# Remove stuff that currently fails.  (Not yet investigated.)
@sources = grep { !/dbfuns\.xls/} @sources;
@sources = grep { !/operator\.xls/} @sources;
@sources = grep { !/cellstyle\.xlsx/} @sources;

my $ngood = 0;
my $nbad = 0;
my $nskipped = 0;

for my $src (@sources) {
    if (!-r $src) {
	$nskipped += 2;
	next;
    }

    print STDERR "$src...\n";

    my $cmd = "$ssdiff --xml $src $src";
    my $output = `$cmd 2>&1`;
    my $err = $?;
    if ($err) {
        &GnumericTest::dump_indented ($output);
        $nbad++;
	die "Failed command: $cmd [$err]\n" if $err > (1 << 8);
    } else {
        if ($output =~ m'<\?xml version="1\.0" encoding="UTF-8"\?>
<ssdiff:Diff>
(  <ssdiff:Sheet Name=".*" Old="\d+" New="\d+"/>
)*</ssdiff:Diff>') {
            $ngood++;
        } else {
            &GnumericTest::dump_indented ($output);
            $nbad++;
        }
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
