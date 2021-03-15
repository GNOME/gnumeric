#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sc importer.");
&test_importer ("$samples/sc/demo_func", "a9f8b22612e481d2a18c9e01404d0980f24a4dff", $mode);
&test_importer ("$samples/sc/demo_math", "c12f26fbab23a57cbfcc9d2e00dd998adc3532ca", $mode);
