#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check the ods importer/exporter with valgrind.");

my $xmllint = &GnumericTest::find_program ("xmllint");
my $unzip = &GnumericTest::find_program ("unzip");

my $format = "Gnumeric_OpenCalc:odf";

my @sources =
    ("$samples/graph-tests.gnumeric",
     "$samples/excel/address.xls",
     "$samples/excel/bitwise.xls",
     "$samples/excel/chart-tests-excel.xls",
     "$samples/excel/datefuns.xls",
     "$samples/excel/dbfuns.xls",
     "$samples/excel/engfuns.xls",
     "$samples/excel/finfuns.xls",
     "$samples/excel/functions.xls",
     "$samples/excel/infofuns.xls",
     "$samples/excel/logfuns.xls",
     "$samples/excel/lookfuns2.xls",
     "$samples/excel/lookfuns.xls",
     "$samples/excel/mathfuns.xls",
     "$samples/excel/objs.xls",
     "$samples/excel/operator.xls",
     "$samples/excel/sort.xls",
     "$samples/excel/statfuns.xls",
     "$samples/excel/textfuns.xls",
     "$samples/excel/yalta2008.xls",
     "$samples/excel12/cellstyle.xlsx",
     "$samples/crlibm.gnumeric",
     "$samples/amath.gnumeric",
     "$samples/gamma.gnumeric",
     "$samples/linest.xls",
     "$samples/vba-725220.xls",
     "$samples/sumif.xls",
     "$samples/array-intersection.xls",
     "$samples/arrays.xls",
     "$samples/docs-samples.gnumeric",
     "$samples/ftest.xls",
     "$samples/ttest.xls",
     "$samples/chitest.xls",
     "$samples/numbermatch.gnumeric",
     "$samples/solver/afiro.mps",
     "$samples/solver/blend.mps",
     "$samples/auto-filter-tests.gnumeric",
     "$samples/cell-comment-tests.gnumeric",
     "$samples/colrow-tests.gnumeric",
     "$samples/cond-format-tests.gnumeric",
     "$samples/formula-tests.gnumeric",
     "$samples/graph-tests.gnumeric",
     "$samples/merge-tests.gnumeric",
     "$samples/names-tests.gnumeric",
     "$samples/number-tests.gnumeric",
     "$samples/object-tests.gnumeric",
     "$samples/page-setup-tests.gnumeric",
     "$samples/rich-text-tests.gnumeric",
     "$samples/sheet-formatting-tests.gnumeric",
     "$samples/solver-tests.gnumeric",
     "$samples/split-panes-tests.gnumeric",
     "$samples/string-tests.gnumeric",
     "$samples/merge-tests.gnumeric",
     "$samples/style-tests.gnumeric",
     "$samples/validation-tests.gnumeric",
    );
my $nskipped = 0;
my $ngood = 0;
my $nbad = 0;

foreach my $src (@sources) {
    if (!-r $src) {
	$nskipped++;
	next;
    }

    my $basename = &File::Basename::fileparse ($src);
    my $basenoext = $basename;
    $basenoext =~ s/\.[^.]+$//;

    my $tmp1 = "$basenoext-tmp.ods";
    print STDERR "$basename -> $tmp1\n";
    &GnumericTest::junkfile ($tmp1);
    my $cmd = &GnumericTest::quotearg ($ssconvert, "-T", $format, $src, $tmp1);
    my $err = &test_valgrind ($cmd, 1, 1);

    if (!$err && $basename !~ /\.ods$/) {
	my $tmp2 = "$basenoext-tmp.gnumeric";
	print STDERR "$tmp1 -> $tmp2\n";
	&GnumericTest::junkfile ($tmp2);

	my $cmd = &GnumericTest::quotearg ($ssconvert, $tmp1, $tmp2);
	$err = &test_valgrind ($cmd, 1, 1);
	&GnumericTest::removejunk ($tmp2);
    }

    $err ? $nbad++ : $ngood++;
    &GnumericTest::removejunk ($tmp1);
}

&GnumericTest::report_skip ("No source files present") if $nbad + $ngood == 0;

if ($nskipped > 0) {
    print STDERR "$nskipped files skipped.\n";
}

if ($nbad > 0) {
    die "Fail\n";
} else {
    print STDERR "Pass\n";
}

# -----------------------------------------------------------------------------
