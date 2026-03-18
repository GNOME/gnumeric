#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $expected;
{ local $/; $expected = <DATA>; }

&message ("Check exponential-smoothing tool.");
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
			  $file, 'exponential-smoothing',
			  ['data' => 'Data!A1:A30',
			   'damp-fact' => 0.5,
			   'es-type' => 0],
			  'A1:A33',
			  $checker);

__DATA__
"Exponential Smoothing"
0.5
"Column 1"
1
1
1.5
2.25
3.125
4.0625
5.03125
6.015625
7.0078125
8.00390625
9.001953125
10.0009765625
11.00048828125
12.000244140625
13.0001220703125
14.00006103515625
15.000030517578125
16.000015258789063
17.00000762939453
18.000003814697266
19.000001907348633
20.000000953674316
21.000000476837158
22.00000023841858
23.00000011920929
24.000000059604645
25.000000029802322
26.00000001490116
27.00000000745058
28.00000000372529
