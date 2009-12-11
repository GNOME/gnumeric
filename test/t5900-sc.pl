#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sc importer.");
&test_importer ("$samples/sc/demo_func", "210675683db9cf06f1dc410f3dc84c69a6767f76", $mode);
&test_importer ("$samples/sc/demo_math", "b3c334cde654b7d40b1fe7631cdf28521484b207", $mode);
