#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sylk importer.");
&test_importer ("$samples/sylk/test.sylk", "90f60d33651e057b617a5e21fd5efab3ad1704f0", $mode);
&test_importer ("$samples/sylk/encoding.sylk", "457029e2e46c3f792111b3e6a02aa007e06c3a67", $mode);
