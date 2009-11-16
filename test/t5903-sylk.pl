#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sylk importer.");
&test_importer ("$samples/sylk/test.sylk", "be8e6e71cc155c1d5c98e18e5f148cb38e653dcc", $mode);
&test_importer ("$samples/sylk/encoding.sylk", "70cc94638fffb58e73888eb48c50de3de5e4b8f7", $mode);
&test_importer ("$samples/sylk/app_b2.sylk", "3717990b16e22312ddbfbd961eb482b54ebafc1e", $mode);
