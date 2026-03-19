#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $expected;
{ local $/; $expected = <DATA>; }

&message ("Check regression tool.");
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
			  $file, 'chi-squared-test',
			  ['data' => 'chisquared!A1:E4',
			   'labels' => 1,
			   'independence' => 1,
			   'alpha' => 0.05],
			  'A1:B5',
			  $checker);

__DATA__
80.53846153846153,
"Test Statistic",24.571202858582602
"Degrees of Freedom",6
p-Value,0.00040984258610966915
"Critical Value",12.59158724374398
