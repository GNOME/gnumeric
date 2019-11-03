#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check that the svg image export works.");

my $xmllint = &GnumericTest::find_program ("xmllint");

my @sources = ("$samples/chart-tests.gnumeric");

my $tmpdir = $0;
$tmpdir =~ s|^.*/||;
$tmpdir =~ s|\.pl$|-tmp|;
if (-e $tmpdir || ($tmpdir =~ m{/}) || ($tmpdir eq '')) {
    print STDERR "$0: unexpected $tmpdir present\n";
    die "Fail\n";
}
END { rmdir $tmpdir; }

if (!mkdir $tmpdir) {
    print STDERR "$0: failed to create $tmpdir\n";
    die "Fail\n";
}

my $nskipped = 0;
my $ngood = 0;
my $nbad = 0;

foreach my $src (@sources) {
    if (!-r $src) {
	$nskipped++;
	next;
    }

    print STDERR "Checking $src\n";

    system ("$ssconvert --export-graphs $src $tmpdir/image-%n.svg");
    my @images = sort <$tmpdir/image-[0-9]*.svg>;
    if (@images == 0) {
	print STDERR "ssconvert failed to produce images in $tmpdir\n";
	die "Fail\n";
    }
    &GnumericTest::junkfile ($_) foreach (reverse @images);

    my $good = 1;
    foreach my $image (@images) {
	my $out = `$xmllint --nonet --noout $image 2>&1`;
	if ($out ne '') {
	    print STDERR "While checking $image:\n";
	    &GnumericTest::dump_indented ($out);
	    $good = 0;
	    last;
	}
	&GnumericTest::removejunk ($image);
    }
    if ($good) {
	print STDERR "Checked ", scalar @images, " generated image files.\n";
	$ngood++;
    } else {
	$nbad++;
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
