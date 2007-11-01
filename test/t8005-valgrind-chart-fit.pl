#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See #492158

&message ("Check some graph fitting with valgrind.");
my $tmp = "chart.xls";
&GnumericTest::junkfile ($tmp);
&test_valgrind ("$ssconvert $samples/chart-smooth-fit-tests.gnumeric $tmp", 1);
