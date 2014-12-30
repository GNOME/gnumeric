#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the mps importer.");
&test_importer ("$samples/solver/blend.mps", "d2965c64019585e2a74a0f2db1bbde4d04feaf30", $mode);
&test_importer ("$samples/solver/afiro.mps", "5ca8608a2a8eafb541d7117fc14624f742a3d955", $mode);
