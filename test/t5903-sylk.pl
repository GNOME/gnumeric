#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sylk importer.");
&test_importer ("$samples/sylk/test.sylk", "6ad511b5edeab30d1ccf500e0ab70f0213ff74eb", $mode);
&test_importer ("$samples/sylk/encoding.sylk", "ce5da9117ad52990f2e7a9c9228d87773bc3c765", $mode);
&test_importer ("$samples/sylk/app_b2.sylk", "cef39c8a1d6db59032679458b3ccfb09dfeb9050", $mode);
