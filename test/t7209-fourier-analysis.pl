#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $expected;
{ local $/; $expected = <DATA>; }

&message ("Check fourier-analysis tool.");
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
			  $file, 'fourier-analysis',
			  ['data' => 'Data!A1:A8'],
			  'A1:B11',
			  $checker);

__DATA__
"Fourier Transform",
"Column 1",
Real,Imaginary
4.5,0
-0.5,1.2071067811865475
-0.5,0.5
-0.5,0.20710678118654746
-0.5,0
-0.49999999999999994,-0.20710678118654746
-0.49999999999999994,-0.5
-0.49999999999999983,-1.2071067811865475
