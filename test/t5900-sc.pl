#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sc importer.");
&test_importer ("$samples/sc/demo_func", "b6ffac4c05a82ae77a4813535323b42befcf520d", $mode);
&test_importer ("$samples/sc/demo_math", "bb49549d4c788b302342696c8133b125eef07920", $mode);
