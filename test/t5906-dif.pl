#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

my $args = { 'mode' => $mode, 'nofont' => 1 };

&message ("Check the dif importer.");
&test_importer ("$samples/dif/sample.dif", "49d24c9f2b92869dfa5e9e2d91aa5ef9a3515b57", $args);
