#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $expected;
{ local $/; $expected = <DATA>; }

&message ("Check wilcoxon-signed-rank-test tool.");
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
			  $file, 'wilcoxon-signed-rank-test',
			  ['data' => 'Data!A1:A30',
			   'median' => 5.0,
			   'alpha' => 0.05],
			  'A1:B10',
			  $checker);

__DATA__
"Wilcoxon Signed Rank Test","Column 1"
Median,15.5
"Predicted Median",5
N,29
S−,18
S+,417
"Test Statistic",18
α,0.05
"P(T≤t) one-tailed",8.424394603027652E-06
"P(T≤t) two-tailed",1.6848789206055303E-05
