#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $expected;
{ local $/; $expected = <DATA>; }

&message ("Check sign-test tool.");
my $file = "$samples/tool-tests.gnumeric";

my %test_opts;
my $checker;
if (($ARGV[0] // '-') eq '--valgrind') {
    $test_opts{'valgrind'} = 1;
    $checker = sub { 1 };  # See http://bugs.kde.org/show_bug.cgi?id=164298
} else {
    $checker = sub { $_[0] eq $expected; };
}

&GnumericTest::test_tool (\%test_opts,
			  $file, 'sign-test',
			  ['data' => 'Data!A1:A30',
			   'median' => 5.0,
			   'alpha' => 0.05],
			  'A1:B8',
			  $checker);

__DATA__
"Sign Test","Column 1"
Median,15.5
"Predicted Median",5
"Test Statistic",4
N,29
α,0.05
"P(T≤t) one-tailed",5.185790359973901E-05
"P(T≤t) two-tailed",0.00010371580719947801
