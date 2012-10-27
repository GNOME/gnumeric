#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the mps importer.");
&test_importer ("$samples/solver/blend.mps", "551a877554b663a983b88f39e2df1becef94283f", $mode);
&test_importer ("$samples/solver/afiro.mps", "3d71a8ce3f9ff12e5e3306f59113aebcdb614816", $mode);
