#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check ssdiff's xml mode");

my $xmllint = &GnumericTest::find_program ("xmllint");

my @sources = &GnumericTest::corpus();

my $nskipped = 0;
my @pairs = ();
@sources = grep { -r $_ ? 1 : ($nskipped++, 0) } @sources;
while (@sources >= 2) {
    my $first = shift @sources;
    my $second = shift @sources;
    push @pairs, [$first,$second];
}

my $ngood = 0;
my $nbad = 0;
for my $p (@pairs) {
    my ($first,$second) = @$p;

    print STDERR "$first vs $second...\n";

    my $cmd = "$ssdiff --xml $first $second | $xmllint --nonet --noout - 2>&1";
    print STDERR "$cmd\n" if $GnumericTest::verbose;
    my $output = `$cmd 2>&1`;
    my $err = $?; # from xmllint
    if ($err) {
        &GnumericTest::dump_indented ($output || '(no output)');
        $nbad++;
	die "Failed command: $cmd [$err]\n" if $err > (1 << 8);
    } else {
        if ($output eq '') {
            $ngood++;
        } else {
            &GnumericTest::dump_indented ($output);
            $nbad++;
        }
    }
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
