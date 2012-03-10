#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sc importer.");
&test_importer ("$samples/sc/demo_func", "425e578f78ddc3a1394779d8a6aeddef659c1ba3", $mode);
&test_importer ("$samples/sc/demo_math", "252684151782961e19a006391a80792d6de86481", $mode);
