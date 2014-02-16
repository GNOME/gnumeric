#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "$samples/string-tests.gnumeric";

&message ("Check string ods roundtrip.");
&test_roundtrip ($file, 'Gnumeric_OpenCalc:odf', "ods");

&message ("Check string xls/BIFF7 roundtrip.");
&test_roundtrip ($file, 'Gnumeric_Excel:excel_biff7', "xls");

&message ("Check string xls/BIFF8 roundtrip.");
&test_roundtrip ($file, 'Gnumeric_Excel:excel_biff8', "xls");

&message ("Check string xlsx roundtrip.");
&test_roundtrip ($file, 'Gnumeric_Excel:xlsx', "xlsx");
