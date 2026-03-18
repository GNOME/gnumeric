#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $expected;
{ local $/; $expected = <DATA>; }

&message ("Check sampling tool.");
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
			  $file, 'sampling',
			  ['data' => 'Data!A1:A30',
			   'periodic' => 1,
			   'period' => 2,
			   'number' => 3,
			   'size' => 5],
			  'A1:C6',
			  $checker);

__DATA__
"Column 1","Column 1","Column 1"
2,2,2
4,4,4
6,6,6
8,8,8
10,10,10
