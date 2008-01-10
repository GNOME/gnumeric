#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

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
