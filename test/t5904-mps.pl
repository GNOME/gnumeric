#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the mps importer.");
&test_importer ("$samples/solver/blend.mps", "3d9ddc9e338d2f29695591c4e39ea9d1cdb56af5", $mode);
&test_importer ("$samples/solver/afiro.mps", "f3abfcb4d8538e23e35c0f948d0136f72dcedef1", $mode);
