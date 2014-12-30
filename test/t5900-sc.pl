#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sc importer.");
&test_importer ("$samples/sc/demo_func", "7faf9490256f2a7ba227bc18bb3e194cd9687eae", $mode);
&test_importer ("$samples/sc/demo_math", "0ad0351b6578e4dd49a46b06528e22d3879fcc2a", $mode);
