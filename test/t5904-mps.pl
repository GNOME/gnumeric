#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the mps importer.");
&test_importer ("$samples/solver/blend.mps", "62f3d23b545b8a40e4891cd8f645167a71cd387d", $mode);
&test_importer ("$samples/solver/afiro.mps", "e07455f4fcfba514b29aa43c550556f48fd19fe1", $mode);
