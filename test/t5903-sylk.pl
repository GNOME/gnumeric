#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sylk importer.");
&test_importer ("$samples/sylk/test.sylk", "9de6e425c421f949f4ae9db66bf9a75da0d4dd3a", $mode);
&test_importer ("$samples/sylk/encoding.sylk", "29492afbf9c25c6662e74329573ded45a5c267c2", $mode);
