#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check that the ods exporter produces valid files.");

my $format = "Gnumeric_OpenCalc:openoffice";
my $schema = $ENV{'HOME'} . "/Download/OpenDocument-v1.2-os-schema.rng";
&GnumericTest::report_skip ("Cannot find schema") unless -r $schema;

my $xmllint = &GnumericTest::find_program ("xmllint");
my $unzip = &GnumericTest::find_program ("unzip");

my @sources =
    ("$samples/excel/address.xls",
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
     # xmllint hangs on these files.  (Well, amath finishes but takes too
     # long.)
     # "$samples/crlibm.gnumeric",
     # "$samples/amath.gnumeric",
     # "$samples/gamma.gnumeric",
     "$samples/numbermatch.gnumeric",
     "$samples/solver/afiro.mps",
     "$samples/solver/blend.mps",
     "$samples/auto-filter-tests.gnumeric",
     "$samples/cell-comment-tests.gnumeric",
     "$samples/colrow-tests.gnumeric",
     "$samples/formula-tests.gnumeric",
     "$samples/number-tests.gnumeric",
     "$samples/page-setup-tests.gnumeric",
     "$samples/sheet-formatting-tests.gnumeric",
     "$samples/split-panes-tests.gnumeric",
     "$samples/string-tests.gnumeric",
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

    print STDERR "Checking $src\n";

    my $tmp = $src;
    $tmp =~ s|^.*/||;
    $tmp =~ s|\..*|.ods|;
    &GnumericTest::junkfile ($tmp);
    system ("$ssconvert -T $format $src $tmp");
    if (!-r $tmp) {
	print STDERR "ssconvert failed to produce $tmp\n";
	die "Fail\n";
    }

    for my $member ('content.xml', 'styles.xml') {
	my $out = `$unzip -p $tmp $member | $xmllint --noout --relaxng $schema - 2>&1`;
	if ($out !~ /^- validates$/) {
	    print STDERR "While checking $member from $tmp:\n";
	    &GnumericTest::dump_indented ($out);
	    $nbad++;
	} else {
	    $ngood++;
	}
    }

    &GnumericTest::removejunk ($tmp);
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
