#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $src1 = "$samples/excel/statfuns.xls";
my $src2 = "$samples/excel/mathfuns.xls";
my $src3 = "$samples/excel/engfuns.xls";
my $src4 = "$samples/auto-filter-tests.gnumeric";

&GnumericTest::report_skip ("Missing source files")
    unless -r $src1 && -r $src2 && -r $src3 && -r $src4;


my %expected;
&setup_expected ();

my $ngood = 0;
my $nbad = 0;

&message ("Checking ssgrep with single file.");
&check ("$ssgrep SUM $src1", 'TEST1A', 1);
&check ("$ssgrep SUM $src2", 'TEST1B', 0);
&check ("$ssgrep SUM $src3", 'TEST1C', 0);

&message ("Checking ssgrep with multiple files.");
&check ("$ssgrep SUM $src1 $src2 $src3", 'TEST1D', 0);

&message ("Checking ssgrep -n.");
&check ("$ssgrep -n SUM $src2", 'TEST2A', 0);

&message ("Checking ssgrep -c.");
&check ("$ssgrep -c SUM $src1", 'TEST3A', 1);
&check ("$ssgrep -c SUM $src2", 'TEST3B' ,0);
&check ("$ssgrep -c SUM $src3", 'TEST3C', 0);

&message ("Checking ssgrep -H.");
&check ("$ssgrep -H SUM $src2", 'TEST4A', 0);

&message ("Checking ssgrep -h.");
&check ("$ssgrep -h SUM $src1 $src2 $src3", 'TEST5A', 0);

&message ("Checking ssgrep with proper regexp.");
&check ("$ssgrep 'SUM[IS]' $src2", 'TEST6A', 0);

&message ("Checking ssgrep with hits on multiple sheets.");
&check ("$ssgrep -T -H -n wbc-gtk-actions $src4", 'TEST7A', 0);

&message ("Checking ssgrep -i.");
&check ("$ssgrep -h -i SUMIF $src1 $src2 $src3", 'TEST8A', 0);

&message ("Checking ssgrep -w.");
&check ("$ssgrep -h -i -w COUNT $src1 $src2 $src3", 'TEST9A', 0);

# -----------------------------------------------------------------------------

if ($nbad > 0) {
    die "Fail\n";
} else {
    print STDERR "Pass\n";
}


sub check {
    my ($cmd,$tag,$ec) = @_;

    my $expected = $expected{$tag};
    die unless defined $expected;

    print STDERR "# $cmd\n" if $GnumericTest::verbose;
    my $output = `$cmd 2>&1`;
    my $err = $?;

    my $bad = 0;
    if ($err != ($ec << 8)) {
	print STDERR "Wrong exit status $err\n";
	$bad++;
    } elsif ($output ne $expected) {
	print STDERR "Wrong output\n";
	$bad++;
    }

    if ($bad) {
	$nbad++;
        &GnumericTest::dump_indented ($output || '(no output)');
    } else {
	$ngood++;
    }
}

sub setup_expected {
    my $tag = undef;
    my $data = undef;

    while (<DATA>) {
	if (/^\*\*\* (\S+) \*\*\*$/) {
	    $expected{$tag} = $data if $tag;
	    $data = '';
	    $tag = $1;
	    next;
	}

	s/\$samples\b/$samples/g;
	$data .= $_;
    }
}

__DATA__
*** TEST1A ***
*** TEST1B ***
SERIESSUM
SUM
SUMIF
SUMPRODUCT
SUMSQ
SUMX2MY2
SUMX2PY2
SUMXMY2
*** TEST1C ***
IMSUM
*** TEST1D ***
$samples/excel/mathfuns.xls:SERIESSUM
$samples/excel/mathfuns.xls:SUM
$samples/excel/mathfuns.xls:SUMIF
$samples/excel/mathfuns.xls:SUMPRODUCT
$samples/excel/mathfuns.xls:SUMSQ
$samples/excel/mathfuns.xls:SUMX2MY2
$samples/excel/mathfuns.xls:SUMX2PY2
$samples/excel/mathfuns.xls:SUMXMY2
$samples/excel/engfuns.xls:IMSUM
*** TEST2A ***
Sheet1!A64:SERIESSUM
Sheet1!A71:SUM
Sheet1!A72:SUMIF
Sheet1!A73:SUMPRODUCT
Sheet1!A74:SUMSQ
Sheet1!A75:SUMX2MY2
Sheet1!A76:SUMX2PY2
Sheet1!A77:SUMXMY2
*** TEST3A ***
0
*** TEST3B ***
8
*** TEST3C ***
1
*** TEST4A ***
$samples/excel/mathfuns.xls:SERIESSUM
$samples/excel/mathfuns.xls:SUM
$samples/excel/mathfuns.xls:SUMIF
$samples/excel/mathfuns.xls:SUMPRODUCT
$samples/excel/mathfuns.xls:SUMSQ
$samples/excel/mathfuns.xls:SUMX2MY2
$samples/excel/mathfuns.xls:SUMX2PY2
$samples/excel/mathfuns.xls:SUMXMY2
*** TEST5A ***
SERIESSUM
SUM
SUMIF
SUMPRODUCT
SUMSQ
SUMX2MY2
SUMX2PY2
SUMXMY2
IMSUM
*** TEST6A ***
SUMIF
SUMSQ
*** TEST7A ***
$samples/auto-filter-tests.gnumeric:cell:Select1!G4:wbc-gtk-actions.c
$samples/auto-filter-tests.gnumeric:cell:Select2!G4:wbc-gtk-actions.c
$samples/auto-filter-tests.gnumeric:cell:SelectNone!G4:wbc-gtk-actions.c
*** TEST8A ***
SUMIF
=sumif(A14:A17,H14)
=sumif(B14:B17,H15)
=sumif(A14:A17,H15,C14:C17)
*** TEST9A ***
COUNT
=count(A14:A17)
=count(G14:G17)
=count(B14:B18)
*** END ***
