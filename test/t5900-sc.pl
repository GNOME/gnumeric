#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sc importer.");
&test_importer ("$samples/sc/demo_func", "7319a011f54056c1bd12d8aba7e14dd5cfa4f414", $mode);
&test_importer ("$samples/sc/demo_math", "9d1cdac42cd7a2576dbc102ef3bf5ba37a162dae", $mode);
