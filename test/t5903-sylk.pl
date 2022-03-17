#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sylk importer.");
&test_importer ("$samples/sylk/test.sylk", "a3ac97c57ab78f01daf3ac9a8dc593f0f610d360", $mode);
&test_importer ("$samples/sylk/encoding.sylk", "72c3358523817b1fb8e04173fb860a1e32c87ca8", $mode);
&test_importer ("$samples/sylk/app_b2.sylk", "d0a8fb5fec00092c6a477a898be7f3e404872c2b", $mode);
