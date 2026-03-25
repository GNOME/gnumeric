#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check auto-format");

my $mode = ((shift @ARGV) || "check");

my $base = 'auto-format.gnumeric';
my $ref = "import-db/$base";
my $tmp = $mode eq 'create-db' ? $ref : &GnumericTest::invent_junkfile ($base);

my $expected_sha1 = '50ac7b9f16ef67fed43e78289ab92d1ce004397e';

my $expected = sub {
    my ($actual) = @_;

    if ($actual =~ /No templates installed/) {
	chomp $actual;
	&report_skip ($actual);
    }

    if (!-r $tmp) {
	print STDERR "$tmp was not created.\n";
	return 0;
    }

    my @normalize = ($PERL, $GnumericTest::normalize_gnumeric);
    my $norm = &GnumericTest::quotearg (@normalize);
    my $zcat = &GnumericTest::quotearg ('zcat', '-f', $tmp);
    my $actual_sha1 = `$zcat | $norm | sha1sum`;
    $actual_sha1 = lc (substr ($actual_sha1, 0, 40));
    die "SHA-1 failure\n" unless $actual_sha1 =~ /^[0-9a-f]{40}$/;

    my $ok = ($actual_sha1 eq $expected_sha1);
    if (!$ok && $mode ne 'update-sha1') {
	print STDERR "New SHA-1 is $actual_sha1; expected was $expected_sha1\n";
    }

    if ($mode eq 'check') {
	return $ok;
    } elsif ($mode eq 'create-db') {
	return $ok;
    } elsif ($mode eq 'diff') {
	die "unimplemented";
    } elsif ($mode eq 'update-sha1') {
	my $script = &GnumericTest::read_file ($0);
	my $count = ($script =~ s/\b$expected_sha1\b/$actual_sha1/g);
	die "SHA-1 found in script $count times\n" unless $count == 1;
	&GnumericTest::update_file ($0, $script);
	$ok = 1;
    } else {
	print STDERR "Invalid mode $mode\n";
	$ok = 0;
    }
    return $ok;
};


unlink $tmp;
&sstest (["test_auto_format", $tmp], $expected);
