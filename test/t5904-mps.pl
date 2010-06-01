#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the mps importer.");
&test_importer ("$samples/solver/blend.mps", "8679808367780bd7c2de7461f990af9c8d2903ea", $mode);
&test_importer ("$samples/solver/afiro.mps", "61a6e6d6dab6da167cb4f4f88baf30b0cb4e4e6a", $mode);
