#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sc importer.");
&test_importer ("$samples/sc/demo_func", "55b8be2c47dbbcd582731c1ff2ac2f54dc756405", $mode);
&test_importer ("$samples/sc/demo_math", "d56bfbf00b2e6316e6077f62ccb93a27bca40595", $mode);
