#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check the core with valgrind.");

my $src = "$samples/excel/statfuns.xls";
&GnumericTest::report_skip ("file $src does not exist") unless -r $src;

my $tmp = "statfuns.gnumeric";
&GnumericTest::junkfile ($tmp);

&test_valgrind ("$ssconvert --recalc $src $tmp", 1);
