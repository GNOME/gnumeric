#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sc importer.");
&test_importer ("$samples/sc/demo_func", "d3e0714a877bc620b7fe2054e28e84fc4fbfefe3", $mode);
&test_importer ("$samples/sc/demo_math", "158cd89eb6c8f9c6169f6f74e8109225a26de1ed", $mode);
