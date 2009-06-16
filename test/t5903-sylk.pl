#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sylk importer.");
&test_importer ("$samples/sylk/test.sylk", "d07aa209202f7be7dd692de662b63e9f023a862b", $mode);
&test_importer ("$samples/sylk/encoding.sylk", "0e694cd12694c4fad49b5eb6c7a83bb7331aa236", $mode);
&test_importer ("$samples/sylk/app_b2.sylk", "8acdf4513b29733d35c90189a235ddc0039a331d", $mode);
