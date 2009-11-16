#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sc importer.");
&test_importer ("$samples/sc/demo_func", "1be918fc16acd996c634cc2e44aaa6caeb158542", $mode);
&test_importer ("$samples/sc/demo_math", "a141d3dd635b8d92e22fad386487491fd29b7513", $mode);
