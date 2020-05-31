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
my $schema_ext_patch = "$topsrc/test/ods-ext-schema.patch";

my $cmd = ($ARGV[0] || '-');
if ($cmd eq 'download') {
    &download ();
    exit 0;
} elsif ($cmd eq 'make-schema-patch') {
    &make_schema_patch ();
    exit 0;
} elsif ($cmd eq 'make-schema-ext') {
    &make_schema_ext ();
    exit 0;
}

my $suggest_download = 0;
if (!-r $schema) {
    print STDERR "Cannot find strict conformance schema\n";
    $schema = undef;
    $suggest_download = 1;
}
if (!-r $schema_ext) {
    print STDERR "Cannot find extended conformance schema\n";
    $schema_ext = undef;
    # This is not a schema supplied by oasis
    $suggest_download = 1;
}
if (!-r $schema_manifest) {
    print STDERR "Cannot find manifest schema\n";
    $schema_manifest = undef;
    $suggest_download = 1;
}

print STDERR "NOTE: Suggest rerunning with argument \"download\" to obtain missing schemas\n"
    if $suggest_download;

my $common_checker = "$xmllint --noout --nonet";

my $checker = $common_checker . ($schema ? " --relaxng $schema" : "");
my $checker_ext = $common_checker . ($schema_ext ? " --relaxng $schema_ext" : "");
my $manifest_checker = $common_checker . ($schema_manifest ? " --relaxng $schema_manifest" : "");
my %checkers = ( 0 => $checker,
		 1 => $checker_ext,
		 2 => $manifest_checker);

my @sources = &GnumericTest::corpus();
# xmllint hangs on these files.  (Well, amath finishes but takes too
# long.)
@sources = grep { !m{(^|/)(amath|crlibm|gamma|numtheory)\.gnumeric$} } @sources;

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
	    if ($out ne '' && $out !~ /^- validates$/) {
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

# -----------------------------------------------------------------------------

sub download {
    my $src = "http://docs.oasis-open.org/office/v1.2/os";

    if (!-d $schemadir) {
	mkdir $schemadir or
	    die "$0: Cannot create directory $schemadir\n";
    }

    my $curl = &GnumericTest::find_program ("curl");
    my $sha1sum = &GnumericTest::find_program ("sha1sum");

    foreach ([scalar &File::Basename::fileparse ($schema),
	      "adc746cbb415ac3a17199442a15b38a5858fc7ef"],
	     [scalar &File::Basename::fileparse ($schema_manifest),
	      "661ab5bc695f9a8266e89cdf2747d8d76eacfedf"],
	) {
	my ($b,$sum) = @$_;

	my $fn = "$schemadir/$b";
	my $tmpfn = "$fn.tmp";

	my $had_it = (-r $fn);
	if ($had_it) {
	    print STDERR "We already have $b\n";
	} else {
	    print STDERR "Downloading $b...\n";
	    unlink $tmpfn;

	    my $cmd = "$curl -s -S -o $tmpfn $src/$b";
	    print STDERR "# $cmd\n";
	    my $code = system ("$cmd 2>&1 | sed -e 's/^/| /' ");
	    &GnumericTest::system_failure ($curl, $code) if $code;
	}

	my $cmd = &GnumericTest::quotearg ($sha1sum, ($had_it ? $fn : $tmpfn));
	my $out = `$cmd 2>&1`;
	die "$0: Unexpected output from $sha1sum\n" unless ($out =~ /^([a-f0-9]{40})\b/i);
	my $act = lc ($1);
	if ($act eq $sum) {
	    if (!$had_it) {
		rename ($tmpfn, $fn) or
		    die "$0: Cannot rename temporary file into place: $!\n";
		print STDERR "Download ok.\n";
	    }
	} else {
	    print STDERR "NOTE: Expected checksum $sum, got $act.\n";
	    if (!$had_it) {
		unlink $tmpfn;
		print STDERR "ERROR: Download failure.\n";
		exit 1;
	    }
	}
    }

    &make_schema_ext () unless -e $schema_ext;
}

# -----------------------------------------------------------------------------

sub make_schema_ext {
    my $dir = "$topsrc/test";
    my $o = length ($dir) + 1;

    if (-e $schema_ext) {
	print STDERR "ERROR: Extended schema already exists.\n";
	print STDERR "If you really want to update it, remove it first.\n";
	exit 1;
    }

    if (!-r $schema || !-r $schema_ext_patch) {
	print STDERR "ERROR: Making extended schema requires both $schema and $schema_ext_patch\n";
	exit 1;
    }

    my $cmd = &GnumericTest::quotearg ("cp", $schema, $schema_ext);
    print STDERR "# $cmd\n";
    system ($cmd);

    $cmd =
	"(cd " . &GnumericTest::quotearg ($dir) .
	" && " .
	&GnumericTest::quotearg ("patch", "-i", substr($schema_ext_patch,$o), substr($schema_ext,$o)) .
	")";
    print STDERR "# $cmd\n";
    system ($cmd);
}

# -----------------------------------------------------------------------------

sub make_schema_patch {
    my $dir = "$topsrc/test";
    my $o = length ($dir) + 1;

    if (!-r $schema || !-r $schema_ext) {
	print STDERR "ERROR: Making patch requires both $schema and $schema_ext\n";
	exit 1;
    }

    my $cmd =
	"(cd " . &GnumericTest::quotearg ($dir) .
	" && " .
	&GnumericTest::quotearg ("diff", "-u", substr($schema,$o), substr($schema_ext,$o)) .
	" >" . &GnumericTest::quotearg (substr($schema_ext_patch,$o)) .
	")";
    print STDERR "# $cmd\n";
    system ($cmd);
}

# -----------------------------------------------------------------------------
