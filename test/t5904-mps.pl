#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the mps importer.");
&test_importer ("$samples/solver/blend.mps", "b934eefa8dcd38d5767ea5b88533fa0a0a0720c2", $mode);
&test_importer ("$samples/solver/afiro.mps", "a665122cce66d5882e60ac86416eb002c49713ac", $mode);
