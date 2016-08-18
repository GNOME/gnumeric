#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $expected;
{ local $/; $expected = <DATA>; }

&message ("Check regression tool.");
my $file = "$samples/tool-tests.gnumeric";

&GnumericTest::test_tool ($file, 'regression',
			  ['x' => 'Data!A1:A30', 'y' => 'Data!B1:B30'],
			  'A1:G18',
			  sub { $_[0] eq $expected; });

__DATA__
"SUMMARY OUTPUT",,"Response Variable","Column 2",,,
,,,,,,
"Regression Statistics",,,,,,
"Multiple R",0.7462523057935172,,,,,
R^2,0.5568925039021411,,,,,
"Standard Error",1.9266332107144821,,,,,
"Adjusted R^2",0.5410672361843604,,,,,
Observations,30,,,,,
,,,,,,
ANOVA,,,,,,
,df,SS,MS,F,"Significance of F",
Regression,1,130.62262009560348,130.62262009560348,35.19008422691248,2.197070097340739E-06,
Residual,28,103.93363480158382,3.7119155286279937,,,
Total,29,234.5562548971873,,,,
,,,,,,
,Coefficients,"Standard Error",t-Statistics,p-Value,0.95,0.95
Intercept,0.4134235230780967,0.7214717512990694,0.5730280116077914,0.5711994028401975,-1.0644443648864401,1.8912914110426335
"Column 1",0.24107897994182365,0.04063957822902626,5.9321230792113955,2.197070097340739E-06,0.15783257765793968,0.3243253822257076
