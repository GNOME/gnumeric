#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check that the ods exporter produces valid files.");

my $xmllint = &GnumericTest::find_program ("xmllint");
my $unzip = &GnumericTest::find_program ("unzip");

my $format = "Gnumeric_OpenCalc:openoffice";
my $format_ext = "Gnumeric_OpenCalc:odf";

my $schemadir = "$topsrc/test/ods-schema";
my $schema = "$schemadir/OpenDocument-v1.2-os-schema.rng";
my $schema_ext = "$schemadir/OpenDocument-v1.2-os-ext-schema.rng";
my $schema_manifest = "$schemadir/OpenDocument-v1.2-os-manifest-schema.rng";

if (($ARGV[0] || '-') eq 'download') {
    &download ();
    exit 0;
}

my $suggest_download = 0;
if (!-r $schema) {
    &message ("Cannot find strict conformance schema");
    $schema = undef;
    $suggest_download = 1;
}
if (!-r $schema_ext) {
    &message ("Cannot find extended conformance schema");
    $schema_ext = undef;
}
if (!-r $schema_manifest) {
    &message ("Cannot find manifest schema");
    $schema_manifest = undef;
    $suggest_download = 1;
}

&message ("Suggest rerunning with argument \"download\" to obtain schemas")
    if $suggest_download;

my $checker = "$xmllint --noout" . ($schema ? " --relaxng $schema" : "");
my $checker_ext = "$xmllint --noout" . ($schema_ext ? " --relaxng $schema_ext" : "");
my $manifest_checker = "$xmllint --noout" . ($schema_manifest ? " --relaxng $schema_manifest" : "");
my %checkers = ( 0 => $checker,
		 1 => $checker_ext,
		 2 => $manifest_checker);

my @sources =
    ("$samples/excel/address.xls",
     "$samples/excel/bitwise.xls",
     "$samples/excel/chart-tests-excel.xls",
     "$samples/excel/datefuns.xls",
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
     # xmllint hangs on these files.  (Well, amath finishes but takes too
     # long.)
     # "$samples/crlibm.gnumeric",
     # "$samples/amath.gnumeric",
     # "$samples/gamma.gnumeric",
     "$samples/linest.xls",
     "$samples/vba-725220.xls",
     "$samples/sumif.xls",
     "$samples/array-intersection.xls",
     "$samples/arrays.xls",
     "$samples/docs-samples.gnumeric",
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
     "$samples/object-tests.gnumeric",
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
	$nskipped += 2;
	next;
    }

    for (my $ext = 0; $ext <= 1; $ext++) {
	print STDERR "Checking $src (", ($ext ? "extended" : "strict"),  " conformance)\n";

	my $tmp = $src;
	$tmp =~ s|^.*/||;
	$tmp =~ s|\..*|.ods|;
	&GnumericTest::junkfile ($tmp);
	my $cmd = "$ssconvert -T " . ($ext ? $format_ext : $format) . " $src $tmp";
	print STDERR "# $cmd\n" if $GnumericTest::verbose;
	system ($cmd);
	if (!-r $tmp) {
	    print STDERR "ssconvert failed to produce $tmp\n";
	    die "Fail\n";
	}

	my %members;
	foreach (`$unzip -v $tmp`) {
	    next unless /^----/ ... /^----/;
	    next unless m{^\s*\d.*\s(\S+)$};
	    my $member = $1;
	    if (exists $members{$member}) {
		print STDERR "Duplicate member $member\n";
		die "Fail\n";
	    }
	    $members{$member} = 1;
	}

	my @check_members = (['content.xml',$ext],
			     ['styles.xml',$ext],
			     ['META-INF/manifest.xml',2],
			     ['settings.xml',$ext],
			     ['meta.xml',$ext]);
	foreach my $member (sort keys %members) {
	    push @check_members, [$member,$ext] if $member =~ m{^Graph\d+/content.xml$};
	}

	for (@check_members) {
	    my ($member,$typ) = @$_;
	    my $this_checker = $checkers{$typ};
	    my $cmd = "$unzip -p $tmp $member | $this_checker --noout -";
	    print STDERR "# $cmd\n" if $GnumericTest::verbose;
	    my $out = `$cmd 2>&1`;
	    if ($out !~ /^- validates$/) {
		print STDERR "While checking $member from $tmp:\n";
		&GnumericTest::dump_indented ($out);
		$nbad++;
	    } else {
		$ngood++;
	    }
	}

	&GnumericTest::removejunk ($tmp);
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

sub download {
    my $src = "http://docs.oasis-open.org/office/v1.2/os";

    if (!-d $schemadir) {
	mkdir (0777, $schemadir) or
	    die "$0: Cannot create directory $schemadir\n";
    }

    my $curl = &GnumericTest::find_program ("curl");

    foreach ([scalar &File::Basename::fileparse ($schema),
	      "adc746cbb415ac3a17199442a15b38a5858fc7ef"],
	     [scalar &File::Basename::fileparse ($schema_manifest),
	      "661ab5bc695f9a8266e89cdf2747d8d76eacfedf"],
	) {
	my ($b,$sha1sum) = @$_;

	my $fn = "$schemadir/$b";
	if (-r $fn) {
	    print STDERR "We already have $b\n";
	    next;
	}

	print STDERR "Downloading $b...\n";
	my $tmpfn = "$fn.tmp";
	unlink $tmpfn;

	my $cmd = "$curl -s -S -o $tmpfn $src/$b";
	print STDERR "# $cmd\n";
	my $code = system ("$cmd 2>&1 | sed -e 's/^/| /' ");
	&GnumericTest::system_failure ($curl, $code) if $code;

	my $out = `sha1sum $tmpfn 2>&1`;
	die "$0: Unexpected output from sha1sum\n" unless ($out =~ /^([a-f0-9]{40})\b/);
	my $act = $1;
	if ($act ne $sha1sum) {
	    unlink $tmpfn;
	    print STDERR "$0: Download failure.\n";
	    print STDERR "$0: Expected checksum $sha1sum, got $act.\n";
	    exit 1;
	}

	rename ($tmpfn, $fn) or
	    die "$0: Cannot rename temporary file into place: $!\n";
	print STDERR "Got it.\n";
    }
}
