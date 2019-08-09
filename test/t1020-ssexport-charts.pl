#!/usr/bin/perl
# -----------------------------------------------------------------------------
# Based on test/t9005-ssconvert-merge.pl
use strict;
use warnings;
use autodie;

use lib ( $0 =~ m|^(.*/)| ? $1 : "." );
use GnumericTest qw/ message test_command $samples $ssexport_charts /;

message("Check ssexport_charts export");

my $src1 = "$samples/fcsolve-with-chart.gnumeric";

my $tmp = "$samples/fcs0.svg";

# Not much of a test, but this at least confirms that the program
# runs with no criticals.
test_command( "$ssexport_charts $src1 $samples/fcs%n.svg",
    sub { return GnumericTest::read_file($tmp) =~ m#<svg#; } );
