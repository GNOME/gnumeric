#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sc importer.");
&test_importer ("$samples/sc/demo_func", "7c7e4b9a6afca7aed84ba1387e4c8aa76e58c191", $mode);
&test_importer ("$samples/sc/demo_math", "be5312fbfced8a714a40b63e886001aab0cbf43b", $mode);
