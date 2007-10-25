#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check the xls importer and exporter with valgrind.");
my $tmp = "statfuns.xls";
&GnumericTest::junkfile ($tmp);
&test_valgrind ("$ssconvert $samples/excel/statfuns.xls $tmp", 1);
