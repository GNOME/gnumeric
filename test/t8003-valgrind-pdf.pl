#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# We get hit by a bitfield error on old Valgrinds.
my $valgrind_version = `valgrind --version 2>&1`;
&GnumericTest::report_skip ("Valgrind is not available")
    unless defined $valgrind_version;
my ($ma,$mi,$rv) = $valgrind_version =~ /^valgrind-?\s*(\d+)\.(\d+)\.(\d+)/;
&GnumericTest::report_skip ("Valgrind is missing or too old")
    unless (($ma || 0) * 1000 + ($mi || 0)) * 1000 + ($rv || 0) > 3001001;

my $cairo = `pkg-config --modversion cairo 2>/dev/null`;
chomp $cairo;
&GnumericTest::report_skip ("Cairo version $cairo is buggy")
    if $cairo eq '1.8.0';

&message ("Check the pdf exporter with valgrind -- part 1.");
my $src = "$samples/excel/statfuns.xls";
&GnumericTest::report_skip ("file $src does not exist") unless -r $src;
my $tmp = "statfuns.pdf";
&GnumericTest::junkfile ($tmp);
&test_valgrind ("$ssconvert $src $tmp", 1);

&message ("Check the pdf exporter with valgrind -- part 2.");
my $src2 = "$samples/excel12/cellstyle.xlsx";
&GnumericTest::report_skip ("file $src2 does not exist") unless -r $src2;
my $tmp2 = "cellstyle.pdf";
&GnumericTest::junkfile ($tmp2);
&test_valgrind ("$ssconvert $src2 $tmp2", 1);
