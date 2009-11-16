#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the applix importer.");
&test_importer ("$samples/applix/sample.as", "1f76742ff7a08f1832361d66ecfbb504f2da9943", $mode);
