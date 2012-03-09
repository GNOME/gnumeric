#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sylk importer.");
&test_importer ("$samples/sylk/test.sylk", "faa924b5cc91f39cdf345550454121ad147bbdd9", $mode);
&test_importer ("$samples/sylk/encoding.sylk", "ecce892b3c4b4f053db506be083efca340258c95", $mode);
&test_importer ("$samples/sylk/app_b2.sylk", "c941c713046301e10ad8bda1b92c1c0262641998", $mode);
