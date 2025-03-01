#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

my $args = { 'mode' => $mode, 'nofont' => 1 };

&message ("Check the sylk importer.");
&test_importer ("$samples/sylk/test.sylk", "e776338d8366eda1daaf5fb7af14befcb0a50ac6", $args);
&test_importer ("$samples/sylk/encoding.sylk", "811348c9ca4a2c1424fd923527c6c8531ae197ae", $args);
&test_importer ("$samples/sylk/app_b2.sylk", "db154e3188aae004981f3d2ce1a5b69692ad5460", $args);
