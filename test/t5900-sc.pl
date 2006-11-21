#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $mode = ((shift @ARGV) || "check");

&test_importer ("$samples/sc/demo_func", "744772546766f80878879d56a329abfdd95d7c4c", $mode);
&test_importer ("$samples/sc/demo_math", "b85dcaf33edd349f20f3d2c71329ff994bc99dca", $mode);
