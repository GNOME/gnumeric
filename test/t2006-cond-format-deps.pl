#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check that conditional format dependency tracking works.");

my $src = "$samples/cond-format-deps.gnumeric";
&GnumericTest::report_skip ("file $src does not exist") unless -r $src;

my $nbad = 0;

&test1 (['A4' => 0], 'C4', "0");
&test1 (['A4' => 1], 'C4', "0.00");

&test1 (['A5' => 0], 'C5', "0");
&test1 (['A5' => 1], 'C5', "0.00");

&test1 (['A5' => 0, 'A6' => 0], 'C6', "0.00");
&test1 (['A5' => 1, 'A6' => 0], 'C6', "0");
&test1 (['A5' => 0, 'A6' => 1], 'C6', "0");
&test1 (['A5' => 1, 'A6' => 1], 'C6', "0.00");

&test1 (['A4' => 0, 'A5' => 0, 'A6' => 0, 'A7' => 0], 'C7', "0");
&test1 (['A4' => 0.5, 'A5' => 0.5, 'A6' => 0.5, 'A7' => 0.5], 'C7', "0.00");
&test1 (['A4' => 0, 'A5' => 0.5, 'A6' => 0.5, 'A7' => 0.5], 'C7', "0");
&test1 (['A4' => 0.5, 'A5' => 0, 'A6' => 0.5, 'A7' => 0.5], 'C7', "0");
&test1 (['A4' => 0.5, 'A5' => 0.5, 'A6' => 0, 'A7' => 0.5], 'C7', "0");
&test1 (['A4' => 0.5, 'A5' => 0.5, 'A6' => 0.5, 'A7' => 0], 'C7', "0");
&test1 (['A3' => 1, 'A4' => 0.5, 'A5' => 0.5, 'A6' => 0.5, 'A7' => 0.5, 'A8' => 1], 'C7', "0.00");

sub test1 {
    my ($set, $rescell, $expected) = @_;

    my @args = ('-T', 'Gnumeric_stf:stf_assistant',
		'-O', 'format=preserve',
		'--recalc', "--export-range=$rescell");
    while (@$set >= 2) {
	my $c = shift @$set;
	my $v = shift @$set;
	push @args, "--set", "$c=$v";
    }

    my $cmd = "$ssconvert " . &GnumericTest::quotearg (@args, $src, 'fd://1');
    print STDERR "# $cmd\n";
    my $result = `$cmd 2>&1`;
    chomp $result;
    if ($result eq $expected) {
    } else {
	print STDERR "No good.  Expected $expected, but got\n";
	for my $line (split ("\n", $result)) {
	    print STDERR "| $line\n";
	}
	$nbad++;
    }
}


if ($nbad > 0) {
    die "Fail\n";
} else {
    print STDERR "Pass\n";
}
