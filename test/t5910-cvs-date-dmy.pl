#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

my $file = "$samples/csv/date-dmy.csv";

&message ("Check the csv importer.");
&test_importer ($file, "d4eba1eae1afc992dee904df9fe2d2e25ce9f50b", $mode);
