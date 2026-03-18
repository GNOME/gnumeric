#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $expected;
{ local $/; $expected = <DATA>; }

&message ("Check normality-test tool.");
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
			  $file, 'normality-test',
			  ['data' => 'Data!A1:A30',
			   'type' => 0],
			  'A1:B6',
			  $checker);

__DATA__
"Anderson-Darling Test","Column 1"
Alpha,0.05
p-Value,0.5147593905756239
Statistic,0.32983300862598125
N,30
Conclusion,"Possibly normal"
