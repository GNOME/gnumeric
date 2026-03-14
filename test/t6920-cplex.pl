#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check the cplex exporter.");

my $tmp = &GnumericTest::invent_junkfile ("junk.cplex");

for my $src ("$samples/solver/afiro.mps",
	     "$samples/solver/blend.mps") {
    &test_command ("$ssconvert $src $tmp", sub { 1 } );
}
