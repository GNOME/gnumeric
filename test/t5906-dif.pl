#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the dif importer.");
&test_importer ("$samples/dif/sample.dif", "e961ef2b1bc75e9a3d6759b5926310f959d129e4", $mode);
