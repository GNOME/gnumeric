#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sylk importer.");
&test_importer ("$samples/sylk/test.sylk", "e908820fa86b3e1021856e549f11ebb46786ecab", $mode);
&test_importer ("$samples/sylk/encoding.sylk", "be9944ed11138b3b0433fe450cfc1ca2bfa5f8a1", $mode);
