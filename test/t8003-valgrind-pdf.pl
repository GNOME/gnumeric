#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check the pdf exporter with valgrind -- part 1.");
my $tmp = "statfuns.pdf";
&GnumericTest::junkfile ($tmp);
&test_valgrind ("$ssconvert $samples/excel/statfuns.xls $tmp", 1);

&message ("Check the pdf exporter with valgrind -- part 2.");
my $tmp2 = "cellstyle.pdf";
&GnumericTest::junkfile ($tmp2);
&test_valgrind ("$ssconvert $samples/excel12/cellstyle.xlsx $tmp2", 1);
