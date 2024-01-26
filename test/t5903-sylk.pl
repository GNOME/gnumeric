#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sylk importer.");
&test_importer ("$samples/sylk/test.sylk", "a157b0e169e74ee4a74cc0eaa3f9c2a778e61cb9", $mode);
&test_importer ("$samples/sylk/encoding.sylk", "e97c6d775b01c1543c55eadc60ccbe35a2b5759d", $mode);
&test_importer ("$samples/sylk/app_b2.sylk", "8a3d99a89177eecc638df0e53b8a579249c8df6b", $mode);
