#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sylk importer.");
&test_importer ("$samples/sylk/test.sylk", "bec1dd78ce89b81b0e1a357717dff8d288499f7b", $mode);
&test_importer ("$samples/sylk/encoding.sylk", "f66dd1c83e281d073725ea7d8979431e61611d4b", $mode);
