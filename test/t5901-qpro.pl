#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the Quattro Pro importer.");
&test_importer ("$samples/qpro/gantt.wb3", "c0e5190ae29734758b950b59c27b39567ce4bf7d", $mode);
