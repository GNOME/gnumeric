#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&test_importer ("$samples/sc/demo_func", "82e84c6e21c4aa3e70d9e05eb5d4d08ca1de810f", $mode);
&test_importer ("$samples/sc/demo_math", "8faad9c99be6a3202cfc5e1ebd77700b2a3264f1", $mode);
