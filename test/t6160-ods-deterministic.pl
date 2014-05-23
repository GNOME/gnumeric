#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check that the ods exporter produces the same results every time.");

my $format = "Gnumeric_OpenCalc:odf";
my $unzip = &GnumericTest::find_program ("unzip");

my @sources =
    ("$samples/excel/address.xls",
     "$samples/excel/bitwise.xls",
     "$samples/excel/chart-tests-excel.xls",
     # "$samples/excel/datefuns.xls", # uses NOW()
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
     # Takes too long
     # "$samples/crlibm.gnumeric",
     "$samples/amath.gnumeric",
     # Takes too long
     # "$samples/gamma.gnumeric",
     "$samples/linest.xls",
     "$samples/vba-725220.xls",
     "$samples/sumif.xls",
     "$samples/array-intersection.xls",
     "$samples/arrays.xls",
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

    print STDERR "Checking $src\n";

    my %members;
    my @tmp;
    foreach my $i (1, 2) {
	my $tmp = $src;
	$tmp =~ s|^.*/||;
	$tmp =~ s|\..*|-$i.ods|;
	&GnumericTest::junkfile ($tmp);
	my $cmd = "$ssconvert -T $format $src $tmp";
	print STDERR "# $cmd\n" if $GnumericTest::verbose;
	system ($cmd);
	if (!-r $tmp) {
	    print STDERR "ssconvert failed to produce $tmp\n";
	    die "Fail\n";
	}

	foreach (`$unzip -v $tmp`) {
	    next unless /^----/ ... /^----/;
	    next unless m{^\s*\d.*\s(\S+)$};
	    my $member = $1;
	    if (($members{$member} || 0) & $i) {
		print STDERR "Duplicate member $member\n";
		die "Fail\n";
	    }
	    $members{$member} += $i;
	}

	push @tmp, $tmp;
    }

    my $tmp1 = $tmp[0];
    my $tmp2 = $tmp[1];

    foreach my $member (sort keys %members) {
	if ($members{$member} != 3) {
	    print STDERR "Member $member is not in both files.\n";
	    $nbad++;
	    next;
	}

	# May contain time stamp.
	next if $member eq 'meta.xml';

	my $cmd1 = "$unzip -p $tmp1 $member";
	print STDERR "# $cmd1\n" if $GnumericTest::verbose;
	my $data1 = `$cmd1`;

	my $cmd2 = "$unzip -p $tmp2 $member";
	print STDERR "# $cmd2\n" if $GnumericTest::verbose;
	my $data2 = `$cmd2`;

	if ($data1 ne $data2) {
	    print STDERR "Member $member is different between two files.\n";
	    $nbad++;
	    next;
	}

	$ngood++;
    }

    &GnumericTest::removejunk ($tmp1);
    &GnumericTest::removejunk ($tmp2);
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
