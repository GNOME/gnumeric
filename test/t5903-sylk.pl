#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sylk importer.");
&test_importer ("$samples/sylk/test.sylk", "484b3510db7bad9ab62f5d668ffd55ac1697b22b", $mode);
&test_importer ("$samples/sylk/encoding.sylk", "004f25ce96e7a2b48d8b4521105b0648f527c779", $mode);
