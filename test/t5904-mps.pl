#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the mps importer.");
&test_importer ("$samples/solver/blend.mps", "18a37ec5fd65514ba9c5326c00d18e8b6a1cd736", $mode);
&test_importer ("$samples/solver/afiro.mps", "6b9eab27a3f0da41d11ac6bb80eb985bf4a10951", $mode);
