#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sylk importer.");
&test_importer ("$samples/sylk/test.sylk", "d152736ea656b2781ab5ab41327956795121dfb0", $mode);
&test_importer ("$samples/sylk/encoding.sylk", "2b584ce7b9bf34a9779a7282151430453978a7a3", $mode);
&test_importer ("$samples/sylk/app_b2.sylk", "c7e8b23c21de380acaae7efa1bfc50b0c6f73318", $mode);
