#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check the gnumeric importer and exporter with valgrind.");
my $tmp = "regress.gnumeric";
&GnumericTest::junkfile ($tmp);
&test_valgrind ("$ssconvert $samples/regress.gnumeric $tmp", 1);
