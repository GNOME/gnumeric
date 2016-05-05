#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the guppi graph importer.");
&test_importer ("$samples/b66666-guppi.gnumeric", "4f9552b87482dfa330ed251dffef467939478244", $mode);
