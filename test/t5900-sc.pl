#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sc importer.");
&test_importer ("$samples/sc/demo_func", "5379732356003b160465d5eacde8345ff4204b2a", $mode);
&test_importer ("$samples/sc/demo_math", "ec51e6cd522ef69777e34f75c7d4e95696568c06", $mode);
