#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the Quattro Pro importer.");
&test_importer ("$samples/qpro/gantt.wb3", "481ba5e66d73a138e942a1bcff286caf304aeada", $mode);
