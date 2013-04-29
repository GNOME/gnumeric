#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the mps importer.");
&test_importer ("$samples/solver/blend.mps", "d60f1d1eade4ae7e534cc4f5088de9cdf4b0c3ca", $mode);
&test_importer ("$samples/solver/afiro.mps", "16b8c1f9ab75d23b3760a128ad72320e893f7a6c", $mode);
