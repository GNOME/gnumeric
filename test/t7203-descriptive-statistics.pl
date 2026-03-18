#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $expected;
{ local $/; $expected = <DATA>; }

&message ("Check descriptive-statistics tool.");
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
			  $file, 'descriptive-statistics',
			  ['data' => 'Data!B1:B30',
			   'k-largest' => 10,
			   'k-smallest' => 2],
			  'A1:B26',
			  $checker);

__DATA__
,"Column 1"
Mean,4.150147712176364
"Standard Error",0.5192349089635848
Median,3.2535144700902423
Mode,#N/A
"Standard Deviation",2.8439667228349674
"Sample Variance",8.088146720592665
Kurtosis,-1.3131098579969223
Skewness,0.3805160334156259
Range,9.038548300176839
Minimum,-0.10112105873840083
Maximum,8.937427241438437
Sum,124.5044313652909
Count,30
,
,
,"Column 1"
"95% CI for the Mean from",3.088193085133964
to,5.212102339218763
,
,"Column 1"
"Largest (10)",6.179169867407796
,
,
,"Column 1"
"Smallest (2)",0.9018303451751581
