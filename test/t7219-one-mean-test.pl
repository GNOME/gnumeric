#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $expected;
{ local $/; $expected = <DATA>; }

&message ("Check one-mean-test tool.");
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
			  $file, 'one-mean-test',
			  ['data' => 'Data!A1:A30',
			   'mean' => 10.0],
			  'A1:B10',
			  $checker);

__DATA__
"Student-t Test","Column 1"
N,30
"Observed Mean",15.5
"Hypothesized Mean",10
"Observed Variance",77.5
"Test Statistic",3.421940592610403
df,29
α,0.05
"P(T≤t) one-tailed",0.0009351747809236465
"P(T≤t) two-tailed",0.001870349561847293
