#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See #492158

&message ("Check some graph fitting with valgrind.");

my $src = "$samples/chart-smooth-fit-tests.gnumeric";
&GnumericTest::report_skip ("file $src does not exist") unless -r $src;

my $tmp = &GnumericTest::invent_junkfile ("chart.xls");

&test_valgrind ("$ssconvert $src $tmp", 1);
