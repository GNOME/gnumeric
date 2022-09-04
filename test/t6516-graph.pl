#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "$samples/graph-tests.gnumeric";
$GnumericTest::default_subtests = '*,-biff7';


if (&subtest ("gnumeric")) {
    &message ("Check graph gnumeric roundtrip.");
    &test_roundtrip ($file,
		     'format' => 'Gnumeric_XmlIO:sax',
		     'ext' => "gnm");
}

if (&subtest ("ods")) {
    &message ("Check graph ods roundtrip.");
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

if (&subtest ("biff7")) {
    # We don't save graphs, so don't test.
    &message ("Check graph xls/BIFF7 roundtrip.");
    &test_roundtrip ($file,
		     'format' => 'Gnumeric_Excel:excel_biff7',
		     'ext' => "xls",
		     'resize' => '16384x256',
		     'filter2' => 'std:drop_codepage');
}

# Point size isn't important.
my $xls_drop_pts_size = "$PERL -p -e '\$_ = \"\" if m{^\\s*<property name=\"(width|height)-pts\">[0-9.]+</property>\\s*}'";

my $xls_missing_marker_shapes = "$PERL -p -e 's/\\bshape=\"(hourglass|butterfly|lefthalf-bar)\"/shape=\"square\"/; s/\\bshape=\"triangle-(down|left|right)\"/shape=\"triangle-up\"/;'";

if (&subtest ("biff8")) {
    &message ("Check graph xls/BIFF8 roundtrip.");
    &test_roundtrip ($file,
		     'format' => 'Gnumeric_Excel:excel_biff8',
		     'ext' => "xls",
		     'filter1' => "$xls_drop_pts_size | $xls_missing_marker_shapes",
		     'filter2' => 'std:drop_codepage',
		     'ignore_failure' => 1);
}

if (&subtest ("xlsx")) {
    &message ("Check graph xlsx roundtrip.");
    &test_roundtrip ($file,
		     'format' => 'Gnumeric_Excel:xlsx',
		     'ext' => "xlsx",
		     'resize' => '1048576x16384',
		     'filter1' => $xls_drop_pts_size,
		     'ignore_failure' => 1);
}
