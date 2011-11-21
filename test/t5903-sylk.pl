#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sylk importer.");
&test_importer ("$samples/sylk/test.sylk", "dbd2df617f51e7882f9a27b034a3c241eb4f4bc7", $mode);
&test_importer ("$samples/sylk/encoding.sylk", "96498527de768bd4cf6f20baf49cd8318e194f78", $mode);
&test_importer ("$samples/sylk/app_b2.sylk", "2de91b711a1217102db891293d0032d3c481b4f8", $mode);
