#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $expected;
{ local $/; $expected = <DATA>; }

&message ("Check principal-components tool.");
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
			  $file, 'principal-components',
			  ['data' => 'Data!A1:B30'],
			  'A1:C18',
			  $checker);

__DATA__
1,,
Covariances,"Column 1","Column 2"
"Column 1",74.91666666666667,18.060833580641617
"Column 2",18.060833580641617,7.818541829906243
,,
Count,30,30
Mean,15.5,4.150147712176364
Variance,77.5,8.088146720592665
,,
Eigenvalues,82.20954031877973,3.378606401812956
Eigenvectors,0.9696688674437338,-0.24442235476810736
,0.24442235476810736,0.9696688674437338
,,
,ξ1,ξ2
"Column 1",0.9986969208307812,-0.051033913460724056
"Column 2",0.77925127811175,0.6267116127543868
,,
"Percent of Trace",0.9605248328037455,0.039475167196254476
