#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $expected;
{ local $/; $expected = <DATA>; }

&message ("Check histogram tool.");
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
			  $file, 'histogram',
			  ['data' => 'Data!A1:A30',
			   'n' => 10,
			   'bin-type' => 0],
			  'A1:C11',
			  $checker);

__DATA__
Histogram,,
,1,"Column 1"
1,4.222222222222222,3
4.222222222222222,7.444444444444445,3
7.444444444444445,10.666666666666668,3
10.666666666666668,13.88888888888889,3
13.88888888888889,17.11111111111111,4
17.11111111111111,20.333333333333336,3
20.333333333333336,23.555555555555557,3
23.555555555555557,26.77777777777778,3
26.77777777777778,30,4
