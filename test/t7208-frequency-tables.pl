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
    &GnumericTest::report_skip("Needs looking into");
}

&GnumericTest::test_tool (\%test_opts,
			  $file, 'frequency-tables',
			  ['data' => 'Data!A1:A30',
			   'n' => 10],
			  'A1:B12',
			  $checker);

__DATA__
