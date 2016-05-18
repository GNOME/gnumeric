#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

my $file = "$samples/csv/date-ymd.csv";

&message ("Check the csv importer.");
&test_importer ($file, "0dc146dcae097d4eece512d157388ed8bb9467b4", $mode);
