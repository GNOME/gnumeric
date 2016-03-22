#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sc importer.");
&test_importer ("$samples/sc/demo_func", "b1868a85945568d907da7dfad34c498cf1209512", $mode);
&test_importer ("$samples/sc/demo_math", "b081197e521cbf973cc309a2c15453f4c8feb55c", $mode);
