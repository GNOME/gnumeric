#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $mode = ((shift @ARGV) || "check");

&test_importer ("$samples/sc/demo_func", "51068c9f192b82b45ac1619f616794c17029b842", $mode);
&test_importer ("$samples/sc/demo_math", "d746da48c1d7d1e7540a95650edb53aa6c0ca066", $mode);
