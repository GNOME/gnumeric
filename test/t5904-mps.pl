#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the mps importer.");
&test_importer ("$samples/solver/blend.mps", "3d3e61c7f892744a1142a044ddd7091295e44133", $mode);
&test_importer ("$samples/solver/afiro.mps", "27fc05dfa9bb5fee724f6f65a1ac35099d5b690e", $mode);
