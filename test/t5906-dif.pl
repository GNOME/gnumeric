#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the dif importer.");
&test_importer ("$samples/dif/sample.dif", "fa0db068f86d909e2a7c9ec6464118b8f3c9a1f4", $mode);
