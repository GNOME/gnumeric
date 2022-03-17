#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the mps importer.");
&test_importer ("$samples/solver/blend.mps", "aaa27556fa3e26bb5abebc42b6eefce78cd2ab07", $mode);
&test_importer ("$samples/solver/afiro.mps", "ecf8aafd4edcd53336799c85deb31bc4ceb7b0c4", $mode);
