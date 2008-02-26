#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sylk importer.");
&test_importer ("$samples/sylk/test.sylk", "72277f372f223e88bb743f0345004b057497d6d2", $mode);
&test_importer ("$samples/sylk/encoding.sylk", "fbe5e755c071e6a2b22b4a37bca7f7c678b039a7", $mode);
