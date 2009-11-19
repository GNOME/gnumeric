#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the mps importer.");
&test_importer ("$samples/solver/blend.mps", "500a4e0a17fcf7ad215594f194a87079dbcba0ac", $mode);
&test_importer ("$samples/solver/afiro.mps", "2d82c1d1d3c6e9d7f5f8f1da6a1d60643945a0bf", $mode);
