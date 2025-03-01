#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

my $args = { 'mode' => $mode, 'nofont' => 1 };

&message ("Check the Quattro Pro importer.");
&test_importer ("$samples/qpro/gantt.wb3", "5ad6ab488ea931bf8fb832384dba3a516fe3de22", $args);
