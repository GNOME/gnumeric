#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the Quattro Pro importer.");
&test_importer ("$samples/qpro/gantt.wb3", "0398afdcc9b48916f7da88bbd20fe57e0eb79c89", $mode);
