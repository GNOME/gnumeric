#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sylk importer.");
&test_importer ("$samples/sylk/test.sylk", "df210b18b69881573dc7814cd0c1809f9f605a5b", $mode);
&test_importer ("$samples/sylk/encoding.sylk", "a795eb09e43b35c054f9b8efd5f8a77649702eb7", $mode);
&test_importer ("$samples/sylk/app_b2.sylk", "77d6c82d9a5713c4a2840a2a0567468563713708", $mode);
