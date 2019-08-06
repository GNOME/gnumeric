#!/usr/bin/perl
# -----------------------------------------------------------------------------
# Based on test/t9005-ssconvert-merge.pl
use strict;
use warnings;
use autodie;

use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest qw/ $ssexport_charts /;

message ("Check ssexport_charts export");

my $src1 = "$samples/fcsolve-with-chart.gnumeric";

my $tmp = "fcs.svg";
GnumericTest::junkfile ($tmp);

# Not much of a test, but this at least confirms that the program
# runs with no criticals.
test_command ("$ssexport_charts $src1 $tmp",
	       sub { return read_file($tmp)=~ m#<svg#; } );
