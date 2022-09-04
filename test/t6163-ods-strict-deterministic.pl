#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check that the strict ods exporter produces the same results every time.");

my $format = "Gnumeric_OpenCalc:openoffice";
my $unzip = &GnumericTest::find_program ("unzip");

my @sources = &GnumericTest::corpus();
# datefuns and docs-samples use NOW(); the rest take too long.
@sources = grep { !m{(^|/)(datefuns\.xls|(docs-samples|crlibm|gamma)\.gnumeric)$} } @sources;

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

	my $cmd1 = &GnumericTest::quotearg ($unzip, "-p", $tmp1, $member);
	print STDERR "# $cmd1\n" if $GnumericTest::verbose;
	my $data1 = `$cmd1`;

	my $cmd2 = &GnumericTest::quotearg ($unzip, "-p", $tmp2, $member);
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
