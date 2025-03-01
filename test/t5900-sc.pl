#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

my $args = { 'mode' => $mode, 'nofont' => 1 };

&message ("Check the sc importer.");
&test_importer ("$samples/sc/demo_func", "054ca74cb329d2eb52d1d7728ad5d3b162c34e8c", $args);
&test_importer ("$samples/sc/demo_math", "29115530871d13a3bbf57c2b220ed6d594a449dc", $args);
