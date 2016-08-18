#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $expected;
{ local $/; $expected = <DATA>; }

&message ("Check anova tool.");
my $file = "$samples/nist/gear.gnumeric";

&GnumericTest::test_tool ($file, 'anova',
			  ['data' => 'Data!A1:j10'],
			  undef,
			  sub { $_[0] eq $expected; });

# FIXME: We really ought to compare with tolerance

__DATA__
"Anova: Single Factor",,,,,,
,,,,,,
SUMMARY,,,,,,
Groups,Count,Sum,Average,Variance,,
"Column 1",10,9.98,0.998,1.8888888888888923E-05,,
"Column 2",10,9.991,0.9991,2.7211111111111157E-05,,
"Column 3",10,9.954,0.9954000000000001,1.582222222222225E-05,,
"Column 4",10,9.982,0.9982,1.4844444444444303E-05,,
"Column 5",10,9.919,0.9919,5.743333333333343E-05,,
"Column 6",10,9.988,0.9987999999999999,9.773333333333265E-05,,
"Column 7",10,10.015,1.0015,6.205555555555568E-05,,
"Column 8",10,10.004,1.0004,1.3155555555555578E-05,,
"Column 9",10,9.983,0.9983000000000001,1.7122222222222252E-05,,
"Column 10",10,9.948,0.9948,2.8400000000000053E-05,,
,,,,,,
,,,,,,
ANOVA,,,,,,
"Source of Variation",SS,df,MS,F,P-value,"F critical"
"Between Groups",0.0007290399999999998,9,8.100444444444442E-05,2.29691241335854,0.022660819278641282,1.985594963730501
"Within Groups",0.0031739999999999963,90,3.5266666666666625E-05,,,
Total,0.003903039999999996,99,,,,
