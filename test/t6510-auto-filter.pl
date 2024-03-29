#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "$samples/auto-filter-tests.gnumeric";

if (&subtest ("gnumeric")) {
    &message ("Check auto-filter gnumeric roundtrip.");
    &test_roundtrip ($file,
		     'format' => 'Gnumeric_XmlIO:sax',
		     'ext' => "gnm");
}

# Something is going on with default widths
if (&subtest ("ods")) {
    &message ("Check auto-filter ods roundtrip.");
    &test_roundtrip ($file,
		     'format' => 'Gnumeric_OpenCalc:odf',
		     'ext' => "ods",
		     'filter2' => 'std:drop_generator',
		     'ignore_failure' => 1);
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

my $xls_greek_filter = "$PERL -p -C7 -e '1 while (s{\\b((Title|Message)=\".*Greek[ ?]+)[^ ?\"]}{\$1?})'";

# Format is deficient
if (&subtest ("biff7")) {
    &message ("Check auto-filter xls/BIFF7 roundtrip.");
    &test_roundtrip ($file,
		     'format' => 'Gnumeric_Excel:excel_biff7',
		     'ext' => "xls",
		     'resize' => '16384x256',
		     'filter1' => $xls_greek_filter,
		     'filter2' => 'std:drop_codepage',
		     'ignore_failure' => 1);
}

if (&subtest ("biff8")) {
    &message ("Check auto-filter xls/BIFF8 roundtrip.");
    &test_roundtrip ($file,
		     'format' => 'Gnumeric_Excel:excel_biff8',
		     'ext' => "xls",
		     'filter2' => 'std:drop_codepage');
}

if (&subtest ("xlsx")) {
    &message ("Check auto-filter xlsx roundtrip.");
    &test_roundtrip ($file,
		     'format' => 'Gnumeric_Excel:xlsx',
		     'ext' => "xlsx",
		     'resize' => '1048576x16384');
}
