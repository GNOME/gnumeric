#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the mps importer.");
&test_importer ("$samples/solver/blend.mps", "dd5e3388a877b1f98f72d19dc3dad81fe8f3cd1a", $mode);
&test_importer ("$samples/solver/afiro.mps", "7d5a4c944000a7db62758cafc1c2aba1942eb518", $mode);
