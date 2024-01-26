#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the mps importer.");
&test_importer ("$samples/solver/blend.mps", "4ddb11e1ca7da13a569d863684a87ede8909b970", $mode);
&test_importer ("$samples/solver/afiro.mps", "10c026e39516f5d75716b331c654e7f03ddfc15c", $mode);
