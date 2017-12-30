#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sylk importer.");
&test_importer ("$samples/sylk/test.sylk", "36e2327a283f6a4783b05187c71380a76148c2a8", $mode);
&test_importer ("$samples/sylk/encoding.sylk", "ad0660a265fedbe51c4fb66609aa06c4215a5611", $mode);
&test_importer ("$samples/sylk/app_b2.sylk", "11203a7d416024cfaa84cb67fab51223ff59b2b5", $mode);
