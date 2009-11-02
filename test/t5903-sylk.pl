#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sylk importer.");
&test_importer ("$samples/sylk/test.sylk", "775eb901ddaa1168ebb25a4099c8c7c5ea371dac", $mode);
&test_importer ("$samples/sylk/encoding.sylk", "4071951eda0d3bde3620a9d54269f5d2fe4c69e4", $mode);
&test_importer ("$samples/sylk/app_b2.sylk", "0137280615937e429bfadb064d620c4cf56d9d1e", $mode);
