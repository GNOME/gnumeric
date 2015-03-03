#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the applix importer.");
&test_importer ("$samples/applix/sample.as", "e9f0b742225fe77c6c3dab4064573e555670be84", $mode);
