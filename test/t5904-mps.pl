#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the mps importer.");
&test_importer ("$samples/solver/blend.mps", "ad55ea8a9e4c23909bb53d46a76ba75e3a2a0450", $mode);
&test_importer ("$samples/solver/afiro.mps", "c6fea9c93c675030d55617a9925b52ba931b5c0e", $mode);
