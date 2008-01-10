#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check the gnumeric importer and exporter with valgrind.");

my $src = "$samples/regress.gnumeric";
&GnumericTest::report_skip ("file $src does not exist") unless -r $src;

my $tmp = "regress.gnumeric";
&GnumericTest::junkfile ($tmp);

&test_valgrind ("$ssconvert $src $tmp", 1);
