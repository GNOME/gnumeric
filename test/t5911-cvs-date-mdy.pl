#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

my $file = "$samples/csv/date-mdy.csv";

&message ("Check the csv importer.");
&test_importer ($file, "55bf9e5f17dc437ea3dce7ee6aef84c203c93729", $mode);
