#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "$samples/selection-tests.gnumeric";

if (&subtest ("gnumeric")) {
    &message ("Check selection gnumeric roundtrip.");
    &test_roundtrip ($file,
		     'format' => 'Gnumeric_XmlIO:sax',
		     'ext' => "gnm");
}

if (&subtest ("ods")) {
    &message ("Check selection ods roundtrip.");
    &test_roundtrip ($file,
		     'format' => 'Gnumeric_OpenCalc:odf',
		     'ext' => "ods",
		     'filter2' => 'std:drop_generator');
}

if (&subtest ("ods-strict")) {
    &message ("Check string ods strict-conformance roundtrip.");
    &test_roundtrip ($file,
		     'format' => 'Gnumeric_OpenCalc:openoffice',
		     'ext' => "ods",
		     'filter1' => 'std:ods_strict',
		     'filter2' => 'std:drop_generator | std:ods_strict',
		     'ignore_failure' => 1);
}

if (&subtest ("biff7")) {
    &message ("Check selection xls/BIFF7 roundtrip.");
    &test_roundtrip ($file,
		     'format' => 'Gnumeric_Excel:excel_biff7',
		     'ext' => "xls",
		     'resize' => '16384x256',
		     'filter2' => 'std:drop_codepage');
}

if (&subtest ("biff8")) {
    &message ("Check selection xls/BIFF8 roundtrip.");
    &test_roundtrip ($file,
		     'format' => 'Gnumeric_Excel:excel_biff8',
		     'ext' => "xls",
		     'filter2' => 'std:drop_codepage');
}

if (&subtest ("xlsx")) {
    &message ("Check selection xlsx roundtrip.");
    &test_roundtrip ($file,
		     'format' => 'Gnumeric_Excel:xlsx',
		     'ext' => "xlsx",
		     'resize' => '1048576x16384');
}
