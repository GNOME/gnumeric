#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check the xls importer and exporter with valgrind.");

my $src = "$samples/ods-with-no-styles.ods";
&GnumericTest::report_skip ("file $src does not exist") unless -r $src;

my $tmp = "ods-with-no-styles.txt";
&GnumericTest::junkfile ($tmp);

&test_valgrind ("$ssconvert $src $tmp", 1);
