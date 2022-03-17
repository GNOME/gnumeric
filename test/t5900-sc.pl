#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sc importer.");
&test_importer ("$samples/sc/demo_func", "b4d30a690b639b3f1dd33c2f333120eb171aa4db", $mode);
&test_importer ("$samples/sc/demo_math", "051bf6597bcd3ba07513aed8b49bf95b6b8f431f", $mode);
