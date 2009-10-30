#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the mps importer.");
&test_importer ("$samples/solver/blend.mps", "5683256cc30c51c907904efea1c65449cb3f558e", $mode);
&test_importer ("$samples/solver/afiro.mps", "3f1d887e91100f67150701257071c8385185d659", $mode);
