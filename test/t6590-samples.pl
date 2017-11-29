#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;
$GnumericTest::default_subtests = '*,-biff7';

my $csvfile = "samples.csv";
&GnumericTest::junkfile ($csvfile);
{
    my $cmd = "$sstest --samples-file=$csvfile";
    print STDERR "# $cmd\n" if $GnumericTest::verbose;
    system ($cmd);
    if (!-r $csvfile) {
	print STDERR "gnumeric failed to produce $csvfile\n";
	die "Fail\n";
    }
}

my $file = "samples.gnumeric";
&GnumericTest::junkfile ($file);
{
    my $cmd = "$ssconvert $csvfile $file";
    print STDERR "# $cmd\n" if $GnumericTest::verbose;
    system ($cmd);
    if (!-r $file) {
	print STDERR "ssconvert failed to produce $file\n";
	die "Fail\n";
    }
}
&GnumericTest::removejunk ($csvfile);


if (&subtest ("gnumeric")) {
    &message ("Check documentation samples gnumeric roundtrip.");
    &test_roundtrip ($file,
		     'format' => 'Gnumeric_XmlIO:sax',
		     'ext' => "gnm");
}

if (&subtest ("ods")) {
    &message ("Check documentation samples ods roundtrip.");

    # Replace calls to boolean functions with constants
    my $bool_func_filter = "$PERL -p -e 's{\\b(true|false)\\(\\)}{uc(\$1)}e'";
    my $concat_filter = "$PERL -p -e 's{\\bconcatenate\\b}{concat} and s{\\baa\\b}{bb}'";

    &test_roundtrip ($file,
		     'format' => 'Gnumeric_OpenCalc:odf',
		     'ext' => "ods",
		     'filter0' => "$bool_func_filter | $concat_filter",
		     'filter2' => 'std:drop_generator',
		     'ignore_failure' => 1);
}

if (&subtest ("biff7")) {
    &message ("Check documentation samples xls/BIFF7 roundtrip.");
    &test_roundtrip ($file,
		     'format' => 'Gnumeric_Excel:excel_biff7',
		     'ext' => "xls",
		     'resize' => '16384x256',
		     'filter2' => 'std:drop_codepage',
		     'ignore_failure' => 1);
}

if (&subtest ("biff8")) {
    &message ("Check documentation samples xls/BIFF8 roundtrip.");
    &test_roundtrip ($file,
		     'format' => 'Gnumeric_Excel:excel_biff8',
		     'ext' => "xls",
		     'filter2' => 'std:drop_codepage',
		     'ignore_failure' => 1);
}

if (&subtest ("xlsx")) {
    &message ("Check documentation samples xlsx roundtrip.");

    # Don't care about cum argument being required in XL.
    my $hypgeom_filter = "$PERL -p -e 'if (/\\bhypgeomdist\\b/) { s{,FALSE\\)}{)}'}";

    &test_roundtrip ($file,
		     'format' => 'Gnumeric_Excel:xlsx',
		     'ext' => "xlsx",
		     'resize' => '1048576x16384',
		     'filter' => $hypgeom_filter,
		     'ignore_failure' => 1);
}
