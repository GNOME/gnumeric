#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the Quattro Pro importer.");
&test_importer ("$samples/qpro/gantt.wb3", "843abc280518bde08c938f1e8800dd7e0a07bc93", $mode);
