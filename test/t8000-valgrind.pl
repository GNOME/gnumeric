#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $tmp = "statfuns.gnumeric";
&GnumericTest::junkfile ($tmp);
&test_valgrind ("$ssconvert $samples/excel/statfuns.xls $tmp", 1);
