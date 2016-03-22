#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the mps importer.");
&test_importer ("$samples/solver/blend.mps", "265ed42b7541b772a62ced0726cb39f023d96b12", $mode);
&test_importer ("$samples/solver/afiro.mps", "c68c4cb622d6fed19555f72225e09cd9f9683226", $mode);
