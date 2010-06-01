#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sylk importer.");
&test_importer ("$samples/sylk/test.sylk", "9ad63953e20d2fde2bd659536959d3b8b2ba268f", $mode);
&test_importer ("$samples/sylk/encoding.sylk", "c88c4e0cf515cf98a3287d2155ff302124c5b849", $mode);
&test_importer ("$samples/sylk/app_b2.sylk", "a4b84d1b0f52c33de3c36bc5b1f7f72c13097643", $mode);
