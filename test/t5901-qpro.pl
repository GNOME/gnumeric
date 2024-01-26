#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the Quattro Pro importer.");
&test_importer ("$samples/qpro/gantt.wb3", "63ece9096e8cc171f9b4c05dd485ba23a05892cc", $mode);
