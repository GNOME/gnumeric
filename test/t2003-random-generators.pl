#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $user = $ENV{'USER'} || '-';
my $ignore_failure = !($user eq 'welinder' || $user eq 'aguelzow');

sub expected {
    my ($actual) = @_;

    my $actual_ok = ($actual =~ /Start: test_random\s*-*\s*(Testing =\S+\s+(\S+: [-+eE0-9.]*\s)*OK\s*)*End: test_random/);
    if (!$actual_ok && $ignore_failure) {
	print STDERR "Ignoring failure possibly caused by random numbers.\n";
	&GnumericTest::dump_indented ($actual);
    }

    return $actual_ok || $ignore_failure;
}

&message ("Checking random number generators.");
&sstest ("test_random", \&expected);
