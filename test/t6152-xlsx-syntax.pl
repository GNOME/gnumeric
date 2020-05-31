#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my %extras =
    ('xml.xsd' => 'http://www.w3.org/2009/01/xml.xsd'
    );


&message ("Check that the xlsx exporter produces valid files.");

my $format = "Gnumeric_Excel:xlsx";
# FIXME: until get figure out how to check xlsx files against a schema,
# this is a very limited test.
my $schema = "$topsrc/test/ooxml-schema/sml.xsd";
if (!-r $schema) {
    &message ("Schema $schema not found");
    $schema = undef;
}
my $chart_schema = "$topsrc/test/ooxml-schema/dml-chart.xsd";
if (!-r $chart_schema) {
    &message ("Schema $chart_schema not found");
    $chart_schema = undef;
}
my $drawing_schema = "$topsrc/test/ooxml-schema/dml-spreadsheetDrawing.xsd";
if (!-r $drawing_schema) {
    &message ("Schema $drawing_schema not found");
    $drawing_schema = undef;
}

my $xmllint_extra = "$topsrc/test/xmllint-extra";
for my $extra (sort keys %extras) {
    my $f = "$xmllint_extra/$extra";
    &GnumericTest::report_skip ("Missing $f available from $extras{$extra}")
	unless -r $f;
}

my $sml_schema_patched_for_comments = undef;
my $sml_schema_patched_for_comments_warned = 0;
if ($schema) {
    system ("grep", "-q", "-w", "CT_Text", $schema);
    $sml_schema_patched_for_comments = ($? == 0);
}

my $xmllint = &GnumericTest::find_program ("xmllint");
my $unzip = &GnumericTest::find_program ("unzip");

my @sources = &GnumericTest::corpus();
# xmllint hangs on these files.  (Well, amath finishes but takes too
# long.)
@sources = grep { !m{(^|/)(amath|crlibm|gamma)\.gnumeric$} } @sources;

my $nskipped = 0;
my $ngood = 0;
my $nbad = 0;

my $common_checker = "$xmllint --noout --nonet --path $xmllint_extra";

my $checker = $common_checker . ($schema ? " --schema $schema" : "");
my $chart_checker = $common_checker . ($chart_schema ? " --schema $chart_schema" : "");
my $drawing_checker = $common_checker . ($drawing_schema ? " --schema $drawing_schema" : "");
my $basic_checker = $common_checker;

my %checkers = ( 0 => $checker,
		 1 => $chart_checker,
		 2 => $drawing_checker,
		 -1 => $basic_checker);

foreach my $src (@sources) {
    if (!-r $src) {
	$nskipped++;
	next;
    }

    print STDERR "Checking $src\n";

    my $tmp = $src;
    $tmp =~ s|^.*/||;
    $tmp =~ s|\..*|.xlsx|;
    &GnumericTest::junkfile ($tmp);

    {
	my $cmd = &GnumericTest::quotearg ($ssconvert, '-T', $format, $src, $tmp);
	print STDERR "# $cmd\n" if $GnumericTest::verbose;
	system ($cmd);
	if (!-r $tmp) {
	    print STDERR "ssconvert failed to produce $tmp\n";
	    die "Fail\n";
	}
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

    my @check_members = (['xl/workbook.xml',0] , ['xl/styles.xml', 0]);
    push @check_members, ['xl/sharedStrings.xml',0] if $members{'xl/sharedStrings.xml'};
    foreach my $member (sort keys %members) {
	if ($member =~ m{^xl/worksheets/sheet\d+\.xml$}) {
	    push @check_members, [$member,0]
	} elsif ($member =~ m{^xl/comments\d+\.xml$}) {
	    if ($sml_schema_patched_for_comments) {
		push @check_members, [$member,0];
	    } else {
		if (!$sml_schema_patched_for_comments_warned) {
		    $sml_schema_patched_for_comments_warned = 1;
		    &message ("Comment checking requires a patched schema, see bug 790756.");
		}
		push @check_members, [$member,-1];
	    }
	} elsif ($member =~ m{^xl/charts/chart\d+\.xml$}) {
	    push @check_members, [$member,1];
	} elsif ($member =~ m{^xl/drawings/drawing\d+\.xml$}) {
	    push @check_members, [$member,2];
	} elsif ($member =~ m{^[-a-zA-Z0-0_/.]+\.xml$}) {
	    push @check_members, [$member,-1];
	} elsif ($member =~ m{^[-a-zA-Z0-0_/.]+\.rels$}) {
	    push @check_members, [$member,-1];
	}
    }

    for (@check_members) {
	my ($member,$typ) = @$_;
	my $this_checker = $checkers{$typ};
	my $cmd = "$unzip -p $tmp '$member' | $this_checker -";
	print STDERR "# $cmd\n" if $GnumericTest::verbose;
	my $out = `$cmd 2>&1`;
	if ($out ne '' && $out !~ /validates$/) {
	    print STDERR "While checking $member from $tmp:\n";
	    &GnumericTest::dump_indented ($out);
	    $nbad++;
	} else {
	    $ngood++;
	}
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
