#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check the lpsolve exporter.");

my $tmp = &GnumericTest::invent_junkfile ("junk.txt");

for my $src ("$samples/solver/afiro.mps",
	     "$samples/solver/blend.mps") {
    &test_command ("$ssconvert -T Gnumeric_lpsolve:lpsolve $src $tmp",
		   sub { 1 } );
}
