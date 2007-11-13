#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sc importer.");
&test_importer ("$samples/sc/demo_func", "c143addb9c71997a48a1e10a93faa292de481f49", $mode);
&test_importer ("$samples/sc/demo_math", "faeb6aacf2a3c588710b1d276248630525761025", $mode);
