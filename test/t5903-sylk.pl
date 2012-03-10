#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sylk importer.");
&test_importer ("$samples/sylk/test.sylk", "b40fd83b35573ee4a1a9c4ecb57ad8423535ef67", $mode);
&test_importer ("$samples/sylk/encoding.sylk", "96752054824a64eb413c40c32e8874be0778114f", $mode);
&test_importer ("$samples/sylk/app_b2.sylk", "17a044cc8e4a2e4873d63ec32c5aab7a0df8834e", $mode);
