#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See #490828

&message ("Check the xls importer and exporter with valgrind.");

my $src = "$samples/excel/sort.xls";
&GnumericTest::report_skip ("file $src does not exist") unless -r $src;

my $tmp = "sort.xls";
&GnumericTest::junkfile ($tmp);

&test_valgrind ("$ssconvert $src $tmp", 1);
