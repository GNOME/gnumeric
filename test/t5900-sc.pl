#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

my $args = { 'mode' => $mode, 'nofont' => 1 };

&message ("Check the sc importer.");
&test_importer ("$samples/sc/demo_func", "b14faf5786d30b497102adfcf9322bd3817c9d6d", $args);
&test_importer ("$samples/sc/demo_math", "f2d9856bd85265953d03592f87c61c2309059983", $args);
