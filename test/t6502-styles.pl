#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "$samples/style-tests.gnumeric";

&message ("Check style gnumeric roundtrip.");
&test_roundtrip ($file, 'Gnumeric_XmlIO:sax', "gnm");

&message ("Check style ods roundtrip.");
&test_roundtrip ($file, 'Gnumeric_OpenCalc:odf', "ods");

&message ("Check style xls/BIFF7 roundtrip.");
&test_roundtrip ($file, 'Gnumeric_Excel:excel_biff7', "xls", '16384x256');

&message ("Check style xls/BIFF8 roundtrip.");
&test_roundtrip ($file, 'Gnumeric_Excel:excel_biff8', "xls");

&message ("Check style xlsx roundtrip.");
&test_roundtrip ($file, 'Gnumeric_Excel:xlsx', "xlsx", '1048576x16384');
