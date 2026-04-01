#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check that the gnumeric exporter produces valid files.");

my $schema = "$topsrc/gnumeric.xsd";
&GnumericTest::report_skip ("Cannot find schema") unless -r $schema;

my $xmllint = &GnumericTest::find_program ("xmllint");

my @sources = &GnumericTest::corpus();

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
    $tmp =~ s|\..*|.xml|;
    $tmp = &GnumericTest::invent_junkfile ($tmp);

    my @cmd1 = ($ssconvert, $src, $tmp);
    print "# ", &GnumericTest::quotearg (@cmd1), "\n" if $GnumericTest::verbose;
    system (@cmd1);
    if (!-r $tmp) {
	print STDERR "ssconvert failed to produce $tmp\n";
	die "Fail\n";
    }

    my @cmd2 = ($xmllint, "--nonet", "--noout", "--schema", $schema, $tmp);
    my $cmd2 = &GnumericTest::quotearg (@cmd2);
    print "# $cmd2\n" if $GnumericTest::verbose;
    my $out = `$cmd2 2>&1`;
    if ($out !~ /validates$/) {
	print STDERR "While checking $tmp:\n";
	&GnumericTest::dump_indented ($out);
	$nbad++;
    } else {
	$ngood++;
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
