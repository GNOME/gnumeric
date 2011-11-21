#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sc importer.");
&test_importer ("$samples/sc/demo_func", "009ba8cb4b63e8656838fd3c28df5f791e670a2c", $mode);
&test_importer ("$samples/sc/demo_math", "3d4b48b3d873b97d66c232136907af08e78257ba", $mode);
