#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $mode = ((shift @ARGV) || "check");

&test_importer ("$samples/sc/demo_func", "8018a950f4b0f4973f40c4bc0a8fda5e0817fb17", $mode);
&test_importer ("$samples/sc/demo_math", "6a12726fe8d14e2d0140ff3648a43f9a043c284c", $mode);
