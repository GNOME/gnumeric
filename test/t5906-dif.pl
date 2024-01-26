#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the dif importer.");
&test_importer ("$samples/dif/sample.dif", "25c16eb30ea4733c0a2053e75a7d8a2ed4c0894e", $mode);
