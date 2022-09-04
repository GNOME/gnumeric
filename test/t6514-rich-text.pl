#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

# NOTE: this is an import/export tests.  We do not look at what is done
# with the formats in the test files.

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "$samples/rich-text-tests.gnumeric";

if (&subtest ("gnumeric")) {
    &message ("Check rich text gnumeric roundtrip.");
    &test_roundtrip ($file,
		     'format' => 'Gnumeric_XmlIO:sax',
		     'ext' => "gnm");
}

if (&subtest ("ods")) {
    &message ("Check rich text ods roundtrip.");
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
		     'filter2' => 'std:drop_generator | std:ods_strict');
}

if (&subtest ("biff7")) {
    &message ("Check rich text xls/BIFF7 roundtrip.");
    &test_roundtrip ($file,
		     'format' => 'Gnumeric_Excel:excel_biff7',
		     'ext' => "xls",
		     'resize' => '16384x256',
		     'filter1' => "std:supersub | std:no_author | std:no_rich_comment",
		     'filter2' => 'std:drop_codepage');
}

if (&subtest ("biff8")) {
    &message ("Check rich text xls/BIFF8 roundtrip.");
    &test_roundtrip ($file,
		     'format' => 'Gnumeric_Excel:excel_biff8',
		     'ext' => "xls",
		     'filter1' => 'std:supersub',
		     'filter2' => 'std:drop_codepage');
}

if (&subtest ("xlsx")) {
    &message ("Check rich text xlsx roundtrip.");
    &test_roundtrip ($file,
		     'format' => 'Gnumeric_Excel:xlsx',
		     'ext' => "xlsx",
		     'resize' => '1048576x16384',
		     'filter1' => 'std:supersub');
}
