#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sc importer.");
&test_importer ("$samples/sc/demo_func", "ac77a0db96f8bfb0aa4076e4908ebf8618a7cb13", $mode);
&test_importer ("$samples/sc/demo_math", "a76a4eb96ea5ec1a14f6983ba3a362516a345619", $mode);
