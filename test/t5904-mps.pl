#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the mps importer.");
&test_importer ("$samples/solver/blend.mps", "1f97ef58ccfb0913ce36dad7b25ad8471e823bc4", $mode);
&test_importer ("$samples/solver/afiro.mps", "6c889e559ecc421b8397fd528dce2514e0cd6540", $mode);
