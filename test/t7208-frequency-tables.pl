#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $expected;
{ local $/; $expected = <DATA>; }

&message ("Check frequency-tables tool.");
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
			  $file, 'frequency-tables',
			  ['data' => 'Data!A1:A30',
			   'n' => 10],
			  'A1:B12',
			  $checker);

__DATA__
"Frequency Table",
Category,"Column 1"
1,1
2,1
3,1
4,1
5,1
6,1
7,1
8,1
9,1
10,1
