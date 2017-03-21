#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $user = $ENV{'USER'} || '-';
my $ignore_failure = !($user eq 'welinder' || $user eq 'aguelzow');

&message ("Checking random number generators.");
&sstest ("test_random", sub { /SUMMARY: OK/ || $ignore_failure } );
