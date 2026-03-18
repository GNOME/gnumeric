#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $expected;
{ local $/; $expected = <DATA>; }

&message ("Check anova tool.");
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
			  $file, 'anova',
			  ['data' => 'anova!A1:C6',
			   'labels' => 1],
			  undef,
			  $checker);

# FIXME: We really ought to compare with tolerance

__DATA__
"Anova: Single Factor",,,,,,
,,,,,,
SUMMARY,,,,,,
Groups,Count,Sum,Average,Variance,,
"Level 1",5,26.7,5.34,1.2480000000000004,,
"Level 2",5,38.6,7.720000000000001,1.2169999999999999,,
"Level 3",5,42.8,8.559999999999999,1.8980000000000001,,
,,,,,,
,,,,,,
ANOVA,,,,,,
"Source of Variation",SS,df,MS,F,P-value,"F critical"
"Between Groups",27.897333333333332,2,13.948666666666666,9.591107036442812,0.0032482226008593005,3.885293834652394
"Within Groups",17.452,12,1.4543333333333335,,,
Total,45.349333333333334,14,,,,
