#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check the core with valgrind.");
my $tmp = "statfuns.gnumeric";
&GnumericTest::junkfile ($tmp);
&test_valgrind ("$ssconvert --recalc $samples/excel/statfuns.xls $tmp", 1);
