#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;
use IO::File;

&message ("Check ssconvert split");

my $N = 3;

# -----------------------------------------------------------------------------
# Create reliable file to split

my @little_files;
for my $i (1 ... $N) {
    my $fn = "t9006-file$i.csv";
    &GnumericTest::junkfile ($fn);
    push @little_files, $fn;
    my $f = new IO::File ($fn, "w");
    die "$0: Failed to write $fn: $!\n" unless $f;
    print $f "File $i,$i,=$i+1\nLine 2,$i,=$i-1\n";
}

my $combined = "t9006-combined.gnumeric";
&GnumericTest::junkfile ($combined);

&test_command ("$ssconvert --merge-to=$combined " . join (" ", @little_files),
	       sub { 1 } );

&GnumericTest::removejunk ($_) foreach @little_files;
@little_files = ();

# -----------------------------------------------------------------------------

sub read_whole_file {
    my ($fn) = @_;
    my $f = new IO::File ($fn, "r");
    die "$0: Failed to read $fn: $!\n" unless $f;
    local $/ = undef;
    my $data = <$f>;
    return $data;
}

sub common_test {
    my ($ext,$args,$post,$pexpected) = @_;
    my $template = 't9006-out-%n.' . $ext;

    &test_command ("$ssconvert --export-file-per-sheet $args $combined '$template'",
		   sub { 1 } );

    for my $i (1 ... $N) {
	my $fn = $template;
	my $im1 = $i - 1;
	$fn =~ s/\%n/$im1/;

	my $expected = $pexpected->[$i - 1];
	my $data = &read_whole_file ($fn);

	if (defined $post) {
	    local $_ = $data;
	    &$post ();
	    $data = $_;
	}

	if ($data ne $expected) {
	    print STDERR "Difference for format $ext, sheet number $i\n";
	    print STDERR "Observed:\n";
	    &GnumericTest::dump_indented ($data);
	    print STDERR "Expected:\n";
	    &GnumericTest::dump_indented ($expected);
	    die "Fail\n";
	}

	unlink $fn;
    }
}

# -----------------------------------------------------------------------------

if (&subtest ("txt")) {
    &message ("Check splitting info text files.");

    for my $sep (",", "::") {
	my @expected;
	for my $i (1 ... $N) {
	    my $ip1 = $i + 1;
	    my $im1 = $i - 1;
	    push @expected, "\"File $i\"$sep$i$sep$ip1\n\"Line 2\"$sep$i$sep$im1\n";
	}

	&common_test ('txt', "-O 'separator=$sep'", undef, \@expected);
    }
}

if (&subtest ("csv")) {
    &message ("Check splitting info csv files.");

    my @expected;
    for my $i (1 ... $N) {
	my $ip1 = $i + 1;
	my $im1 = $i - 1;
	push @expected, "\"File $i\",$i,$ip1\n\"Line 2\",$i,$im1\n";
    }

    &common_test ('csv', '', undef, \@expected);
}

if (&subtest ("tex")) {
    &message ("Check splitting info latex files.");

    my @expected;
    for my $i (1 ... $N) {
	my $ip1 = $i + 1;
	my $im1 = $i - 1;
	push @expected, "File $i\t&$i\t&$ip1\\\\\nLine 2\t&$i\t&$im1\\\\\n";
    }

    &common_test ('tex', '-T Gnumeric_html:latex_table',
		  sub { s/(\%.*$)\n//mg; },
		  \@expected);
}

if (&subtest ("pdf")) {
    &message ("Check splitting info pdf files.");

    # We check only that the files are generated
    my @expected = ('') x $N;
    &common_test ('pdf', '', sub { $_ = ''; }, \@expected);
}
