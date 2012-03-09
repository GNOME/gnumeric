#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sc importer.");
&test_importer ("$samples/sc/demo_func", "b228d59793c7ae43de9f7fbf2142d2156a704681", $mode);
&test_importer ("$samples/sc/demo_math", "1dfe0a8e3bace2c5881198919570c0c983dcf480", $mode);
