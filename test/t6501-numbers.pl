#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "$samples/number-tests.gnumeric";

&message ("Check number gnumeric roundtrip.");
&test_roundtrip ($file, 'Gnumeric_XmlIO:sax', "gnm");

&message ("Check number ods roundtrip.");
&test_roundtrip ($file, 'Gnumeric_OpenCalc:odf', "ods");

&message ("Check number xls/BIFF7 roundtrip.");
&test_roundtrip ($file, 'Gnumeric_Excel:excel_biff7', "xls", '16384x256');

&message ("Check number xls/BIFF8 roundtrip.");
&test_roundtrip ($file, 'Gnumeric_Excel:excel_biff8', "xls");

&message ("Check number xlsx roundtrip.");
&test_roundtrip ($file, 'Gnumeric_Excel:xlsx', "xlsx", '1048576x16384');
