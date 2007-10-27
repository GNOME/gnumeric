#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See #490828

&message ("Check the xls importer and exporter with valgrind.");
my $tmp = "sort.xls";
&GnumericTest::junkfile ($tmp);
&test_valgrind ("$ssconvert $samples/excel/sort.xls $tmp", 1);
