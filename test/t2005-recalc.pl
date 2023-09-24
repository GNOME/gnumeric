#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check that dependency tracking works.");

$GnumericTest::default_corpus = 'random:20';
my @sources = &GnumericTest::corpus();
push @sources, &GnumericTest::corpus('/recalc/');
# Must avoid volatile functions
@sources = grep { !m{(^|/)(chart-tests\.gnumeric|datefuns\.xls|vba-725220\.xls|docs-samples\.gnumeric|numbermatch\.gnumeric)$} } @sources;
# Avoid slow stuff
@sources = grep { !m{(^|/)(address\.xls|bitwise\.xls|operator\.xls|linest\.xls)$} } @sources;
@sources = grep { !m{(^|/)(amath\.gnumeric|gamma\.gnumeric|crlibm\.gnumeric|ilog\.gnumeric)$} } @sources;
@sources = grep { !m{(^|/)(numtheory\.gnumeric)$} } @sources;
# Currently fails, pending investigation
@sources = grep { !m{(^|/)(arrays\.xls)$} } @sources;

my $nskipped = 0;
my $ngood = 0;
my $nbad = 0;

foreach my $src (@sources) {
    if (!-r $src) {
	$nskipped++;
	next;
    }

    print STDERR "Checking $src\n";

    my $cmd = &GnumericTest::quotearg ($sstest, 'test_recalc', $src);
    my $actual = `$cmd 2>&1`;
    my $err = $?;
    if ($err) {
	$nbad++;
	next;
    }

    if ($actual =~ /^Changing the contents of \d+ cells, one at a time\.\.\.$/) {
	$ngood++;
	next;
    }

    my @lines = split ("\n", $actual);
    my $toolong = (@lines > 25);
    splice @lines, 25 if $toolong;
    foreach (@lines) {
	print "| $_\n";
    }
    print "...\n" if $toolong;
    $nbad++;
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
