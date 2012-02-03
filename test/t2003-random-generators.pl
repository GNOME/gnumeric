#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

sub expected {
    my ($actual) = @_;

    return $actual =~ /Start: test_random\s*-*\s*(Testing =\S+\s+(\S+: [-+eE0-9.]*\s)*OK\s*)*End: test_random/;
}

&message ("Checking random number generators.");
&sstest ("test_random", \&expected);
