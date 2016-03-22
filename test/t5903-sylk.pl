#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sylk importer.");
&test_importer ("$samples/sylk/test.sylk", "86c7c82fe0425ddcaaa264b50f9b5c335e6c48f6", $mode);
&test_importer ("$samples/sylk/encoding.sylk", "265e5809772cd041378ee14f0518a8ded9d97554", $mode);
&test_importer ("$samples/sylk/app_b2.sylk", "5c6c1a2d4d82761f2927b1c0f221d6d295886c20", $mode);
