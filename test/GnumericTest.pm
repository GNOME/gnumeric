package GnumericTest;
use strict;
use Exporter;
use File::Basename qw(fileparse);
use Config;
use XML::Parser;

$| = 1;

@GnumericTest::ISA = qw (Exporter);
@GnumericTest::EXPORT = qw(test_sheet_calc test_valgrind
                           test_importer test_exporter test_roundtrip
			   test_ssindex sstest test_command message
			   $ssconvert $sstest $topsrc $top_builddir
			   $samples $PERL);
@GnumericTest::EXPORT_OK = qw(junkfile);

use vars qw($topsrc $top_builddir $samples $PERL $verbose);
use vars qw($ssconvert $ssindex $sstest);
use vars qw($normalize_gnumeric);

$PERL = $Config{'perlpath'};
$PERL .= $Config{'_exe'} if $^O ne 'VMS' && $PERL !~ m/$Config{'_exe'}$/i;

$topsrc = $0;
$topsrc =~ s|/[^/]+$|/..|;
$topsrc =~ s|/test/\.\.$||;

$top_builddir = "..";
$samples = "$topsrc/samples"; $samples =~ s{^\./}{};
$ssconvert = "$top_builddir/src/ssconvert";
$ssindex = "$top_builddir/src/ssindex";
$sstest = "$top_builddir/src/sstest";
$normalize_gnumeric = "$PERL $topsrc/test/normalize-gnumeric";
$verbose = 0;

# -----------------------------------------------------------------------------

my @tempfiles;
END {
    unlink @tempfiles;
}

sub junkfile {
    my ($fn) = @_;
    push @tempfiles, $fn;
}

sub removejunk {
    my ($fn) = @_;
    unlink $fn;

    if (@tempfiles && $fn eq $tempfiles[-1]) {
	scalar (pop @tempfiles);
    }
}

# -----------------------------------------------------------------------------

sub system_failure {
    my ($program,$code) = @_;

    if ($code == -1) {
	die "failed to run $program: $!\n";
    } elsif ($code >> 8) {
	my $sig = $code >> 8;
	die "$program died due to signal $sig\n";
    } else {
	die "$program exited with exit code $code\n";
    }
}

sub read_file {
    my ($fn) = @_;

    local (*FIL);
    open (FIL, $fn) or die "Cannot open $fn: $!\n";
    local $/ = undef;
    my $lines = <FIL>;
    close FIL;

    return $lines;
}

sub write_file {
    my ($fn,$contents) = @_;

    local (*FIL);
    open (FIL, ">$fn.tmp") or die "Cannot create $fn.tmp: $!\n";
    print FIL $contents;
    close FIL;
    rename "$fn.tmp", $fn;
}

sub update_file {
    my ($fn,$contents) = @_;

    my @stat = stat $fn;
    die "Cannot stat $fn: $!\n" unless @stat > 2;

    &write_file ($fn,$contents);

    chmod $stat[2], $fn or
	die "Cannot chmod $fn: $!\n";
}

# Print a string with each line prefixed by "| ".
sub dump_indented {
    my ($txt) = @_;
    return if $txt eq '';
    $txt =~ s/^/| /gm;
    $txt = "$txt\n" unless substr($txt, -1) eq "\n";
    print STDERR $txt;
}

sub find_program {
    my ($p) = @_;

    if ($p =~ m{/}) {
	return $p if -x $p;
    } else {
	my $PATH = exists $ENV{'PATH'} ? $ENV{'PATH'} : '';
	foreach my $dir (split (':', $PATH)) {
	    $dir = '.' if $dir eq '';
	    my $tentative = "$dir/$p";
	    return $tentative if -x $tentative;
	}
    }

    &report_skip ("$p is missing");
}

# -----------------------------------------------------------------------------

sub message {
    my ($message) = @_;
    print "-" x 79, "\n";
    my $me = $0;
    $me =~ s|^.*/||;
    foreach (split (/\n/, $message)) {
	print "$me: $_\n";
    }
}

# -----------------------------------------------------------------------------

sub test_command {
    my ($cmd,$test) = @_;

    my $output = `$cmd 2>&1`;
    my $err = $?;
    die "Failed command: $cmd\n" if $err;

    &dump_indented ($output);
    local $_ = $output;
    if (&$test ($output)) {
	print STDERR "Pass\n";
    } else {
	die "Fail\n";
    }
}

# -----------------------------------------------------------------------------

sub sstest {
    my $test = shift @_;
    my $expected = shift @_;

    my $cmd = "$sstest $test";
    my $actual = `$cmd 2>&1`;
    my $err = $?;
    die "Failed command: $cmd\n" if $err;

    my $ok;
    if (ref $expected) {
	local $_ = $actual;
	$ok = &$expected ($_);
	if (!$ok) {
	    foreach (split ("\n", $actual)) {
		print "| $_\n";
	    }
	}
    } else {
	my @actual = split ("\n", $actual);
	chomp @actual;
	while (@actual > 0 && $actual[-1] eq '') {
	    my $dummy = pop @actual;
	}

	my @expected = split ("\n", $expected);
	chomp @expected;
	while (@expected > 0 && $expected[-1] eq '') {
	    my $dummy = pop @expected;
	}

	my $i = 0;
	while ($i < @actual && $i < @expected) {
	    last if $actual[$i] ne $expected[$i];
	    $i++;
	}
	if ($i < @actual || $i < @expected) {
	    $ok = 0;
	    print STDERR "Differences between actual and expected on line ", ($i + 1), ":\n";
	    print STDERR "Actual  : ", ($i < @actual ? $actual[$i] : "-"), "\n";
	    print STDERR "Expected: ", ($i < @expected ? $expected[$i] : "-"), "\n";
	} else {
	    $ok = 1;
	}
    }

    if ($ok) {
	print STDERR "Pass\n";
    } else {
	die "Fail.\n\n";
    }
}

# -----------------------------------------------------------------------------

sub test_sheet_calc {
    my $file = shift @_;
    my $pargs = (ref $_[0]) ? shift @_ : [];
    my ($range,$expected) = @_;

    &report_skip ("file $file does not exist") unless -r $file;

    my $tmp = fileparse ($file);
    $tmp =~ s/\.[a-zA-Z0-9]+$/.csv/;
    &junkfile ($tmp);

    my $code = system ("$ssconvert " .
		       join (" ", @$pargs) .
		       " --recalc --export-range='$range' '$file' '$tmp' 2>&1 | sed -e 's/^/| /'");
    &system_failure ($ssconvert, $code) if $code;

    my $actual = &read_file ($tmp);

    my $ok;
    if (ref $expected) {
	local $_ = $actual;
	$ok = &$expected ($_);
    } else {
	$ok = ($actual eq $expected);
    }

    &removejunk ($tmp);

    if ($ok) {
	print STDERR "Pass\n";
    } else {
	$actual =~ s/\s+$//;
	&dump_indented ($actual);
	die "Fail.\n\n";
    }
}

# -----------------------------------------------------------------------------

my $import_db = 'import-db';

# Modes:
#   check: check that conversion produces right file
#   create-db: save the current corresponding .gnumeric
#   diff: diff conversion against saved .gnumeric
#   update-SHA-1: update $0 to show current SHA-1  [validate first!]

sub test_importer {
    my ($file,$sha1,$mode) = @_;

    my $tmp = fileparse ($file);
    ($tmp =~ s/\.[a-zA-Z0-9]+$/.gnumeric/ ) or ($tmp .= '.gnumeric');
    if ($mode eq 'create-db') {
	-d $import_db or mkdir ($import_db, 0777) or
	    die "Cannot create $import_db: $!\n";
	$tmp = "$import_db/$tmp";
    } else {
	&junkfile ($tmp);
    }

    &report_skip ("file $file does not exist") unless -r $file;

    my $code = system ("$ssconvert '$file' '$tmp' 2>&1 | sed -e 's/^/| /'");
    &system_failure ($ssconvert, $code) if $code;

    my $htxt = `zcat -f '$tmp' | $normalize_gnumeric | sha1sum`;
    my $newsha1 = lc substr ($htxt, 0, 40);
    die "SHA-1 failure\n" unless $newsha1 =~ /^[0-9a-f]{40}$/;

    if ($mode eq 'check') {
	if ($sha1 ne $newsha1) {
	    die "New SHA-1 is $newsha1; expected was $sha1\n";
	}
	print STDERR "Pass\n";
    } elsif ($mode eq 'create-db') {
	if ($sha1 ne $newsha1) {
	    warn ("New SHA-1 is $newsha1; expected was $sha1\n");
	}
	# No file to remove
	return;
    } elsif ($mode eq 'diff') {
	my $saved = "$import_db/$tmp";
	die "$saved not found\n" unless -r $saved;

	my $tmp1 = "$tmp-old";
	&junkfile ($tmp1);
	my $code1 = system ("zcat -f '$saved' >'$tmp1'");
	&system_failure ('zcat', $code1) if $code1;

	my $tmp2 = "$tmp-new";
	&junkfile ($tmp2);
	my $code2 = system ("zcat -f '$tmp' >'$tmp2'");
	&system_failure ('zcat', $code2) if $code2;

	my $code3 = system ('diff', @ARGV, $tmp1, $tmp2);

	&removejunk ($tmp2);
	&removejunk ($tmp1);
    } elsif ($mode =~ /^update-(sha|SHA)-?1/) {
	if ($sha1 ne $newsha1) {
	    my $script = &read_file ($0);
	    my $count = ($script =~ s/\b$sha1\b/$newsha1/g);
	    die "SHA-1 found in script $count times\n" unless $count == 1;
	    &update_file ($0, $script);
	}
	return;
    } else {
	die "Invalid mode \"$mode\"\n";
    }

    &removejunk ($tmp);
}

# -----------------------------------------------------------------------------

sub test_exporter {
    my ($file) = @_;

    &report_skip ("file $file does not exist") unless -r $file;

    my $tmp = fileparse ($file);
    $tmp =~ s/\.([a-zA-Z0-9]+)$// or die "Must have extension for export test.";
    my $ext = $1;
    my $code;
    my $keep = 0;

    my $tmp1 = "$tmp.gnumeric";
    &junkfile ($tmp1) unless $keep;
    {
	my $cmd = "$ssconvert '$file' '$tmp1'";
	print STDERR "# $cmd\n" if $verbose;
	my $code = system ("$cmd 2>&1 | sed -e 's/^/| /'");
	&system_failure ($ssconvert, $code) if $code;
    }

    my $tmp2 = "$tmp-new.$ext";
    &junkfile ($tmp2) unless $keep;
    {
	my $cmd = "$ssconvert '$file' '$tmp2'";
	print STDERR "# $cmd\n" if $verbose;
	my $code = system ("$cmd 2>&1 | sed -e 's/^/| /'");
	&system_failure ($ssconvert, $code) if $code;
    }

    my $tmp3 = "$tmp-new.gnumeric";
    &junkfile ($tmp3) unless $keep;
    {
	my $cmd = "$ssconvert '$tmp2' '$tmp3'";
	print STDERR "# $cmd\n" if $verbose;
	my $code = system ("$cmd 2>&1 | sed -e 's/^/| /'");
	&system_failure ($ssconvert, $code) if $code;
    }

    my $tmp4 = "$tmp.xml";
    &junkfile ($tmp4) unless $keep;
    $code = system ("zcat -f '$tmp1' >'$tmp4'");
    &system_failure ('zcat', $code) if $code;

    my $tmp5 = "$tmp-new.xml";
    &junkfile ($tmp5) unless $keep;
    $code = system ("zcat -f '$tmp3' >'$tmp5'");
    &system_failure ('zcat', $code) if $code;

    $code = system ('diff', '-u', $tmp4, $tmp5);
    &system_failure ('diff', $code) if $code;

    print STDERR "Pass\n";
}

# -----------------------------------------------------------------------------

# The BIFF formats leave us with a msole:codepage property
my $drop_codepage_filter =
    "$PERL -p -e '\$_ = \"\" if m{<meta:user-defined meta:name=.msole:codepage.}'";

my $drop_generator_filter =
    "$PERL -p -e '\$_ = \"\" if m{<meta:generator>}'";

# BIFF7 doesn't store cell comment author
my $no_author_filter = "$PERL -p -e 's{ Author=\"[^\"]*\"}{};'";

# BIFF7 cannot store rich text comments
my $no_rich_comment_filter = "$PERL -p -e 'if (/gnm:CellComment/) { s{ TextFormat=\"[^\"]*\"}{}; }'";

# Excel cannot have superscript and subscript at the same time
my $supersub_filter = "$PERL -p -e 's{\\[superscript=1:(\\d+):(\\d+)\\]\\[subscript=1:(\\d+):\\2\\]}{[superscript=1:\$1:\$3][subscript=1:\$3:\$2]};'";


sub normalize_filter {
    my ($f) = @_;
    return 'cat' unless defined $f;

    $f =~ s/std:drop_codepage/$drop_codepage_filter/;
    $f =~ s/std:drop_generator/$drop_generator_filter/;
    $f =~ s/std:no_author/$no_author_filter/;
    $f =~ s/std:no_rich_comment/$no_rich_comment_filter/;
    $f =~ s/std:supersub/$supersub_filter/;

    return $f;
}

# -----------------------------------------------------------------------------

sub test_roundtrip {
    my ($file,%named_args) = @_;

    &report_skip ("file $file does not exist") unless -r $file;

    my $format = $named_args{'format'};
    my $newext = $named_args{'ext'};
    my $resize = $named_args{'resize'};
    my $ignore_failure = $named_args{'ignore_failure'};

    my $filter1 = &normalize_filter ($named_args{'filter1'} ||
				     $named_args{'filter'});
    my $filter2 = &normalize_filter ($named_args{'filter2'} ||
				     $named_args{'filter'});

    my $tmp = fileparse ($file);
    $tmp =~ s/\.([a-zA-Z0-9]+)$// or die "Must have extension for roundtrip test.";
    my $ext = $1;
    my $code;
    my $keep = 0;

    my $file_resized = $file;
    if ($resize) {
	$file_resized =~ s{^.*/}{};
	$file_resized =~ s/(\.gnumeric)$/-resize$1/;
	unlink $file_resized;
	my $cmd = "$ssconvert --resize $resize '$file' '$file_resized'";
	print STDERR "# $cmd\n" if $verbose;
	$code = system ("$cmd 2>&1 | sed -e 's/^/| /'");
	&system_failure ($ssconvert, $code) if $code;
	die "Failed to produce $file_resized\n" unless -r $file_resized;
	&junkfile ($file_resized) unless $keep;
    }
    
    my $tmp1 = "$tmp.$newext";
    unlink $tmp1;
    &junkfile ($tmp1) unless $keep;
    {
	my $cmd = "$ssconvert -T $format '$file_resized' '$tmp1'";
	print "# $cmd\n" if $verbose;
	my $code = system ("$cmd 2>&1 | sed -e 's/^/| /'");
	&system_failure ($ssconvert, $code) if $code;
	die "Failed to produce $tmp1\n" unless -r $tmp1;
    }

    my $tmp2 = "$tmp-new.$ext";
    unlink $tmp2;
    &junkfile ($tmp2) unless $keep;
    {
	my $cmd = "$ssconvert '$tmp1' '$tmp2'";
	print "# $cmd\n" if $verbose;
	my $code = system ("$cmd 2>&1 | sed -e 's/^/| /'");
	&system_failure ($ssconvert, $code) if $code;
	die "Failed to produce $tmp2\n" unless -r $tmp2;
    }

    my $tmp_xml = "$tmp.xml";
    unlink $tmp_xml;
    &junkfile ($tmp_xml) unless $keep;
    $code = system ("zcat -f '$file_resized' | $normalize_gnumeric | $filter1 >'$tmp_xml'");
    &system_failure ('zcat', $code) if $code;

    my $tmp2_xml = "$tmp-new.xml";
    unlink $tmp2_xml;
    &junkfile ($tmp2_xml) unless $keep;
    # print STDERR "zcat -f '$tmp2' | $normalize_gnumeric | $filter2 >'$tmp2_xml'\n";
    $code = system ("zcat -f '$tmp2' | $normalize_gnumeric | $filter2 >'$tmp2_xml'");
    &system_failure ('zcat', $code) if $code;

    $code = system ('diff', '-u', $tmp_xml, $tmp2_xml);
    &system_failure ('diff', $code) if $code && !$ignore_failure;

    print STDERR "Pass\n";
}

# -----------------------------------------------------------------------------

sub test_valgrind {
    my ($cmd,$uselibtool) = @_;

    local (%ENV) = %ENV;
    $ENV{'G_DEBUG'} .= ':gc-friendly:resident-modules';
    $ENV{'G_SLICE'} = 'always-malloc';
    delete $ENV{'VALGRIND_OPTS'};

    my $outfile = 'valgrind.log';
    unlink $outfile;
    die "Cannot remove $outfile.\n" if -f $outfile;
    &junkfile ($outfile);

    my $valhelp = `valgrind --help 2>&1`;
    &report_skip ("Valgrind is not available") unless defined $valhelp;
    die "Problem running valgrind.\n" unless $valhelp =~ /log-file/;

    my $valvers = `valgrind --version`;
    die "Problem running valgrind.\n"
	unless $valvers =~ /^valgrind-(\d+)\.(\d+)\.(\d+)/;
    $valvers = $1 * 10000 + $2 * 100 + $3;
    &report_skip ("Valgrind is too old") unless $valvers >= 30500;

    $cmd = "--gen-suppressions=all $cmd";

    {
	my $suppfile = "$topsrc/test/common.supp";
	&report_skip ("file $suppfile does not exist") unless -r $suppfile;
	$cmd = "--suppressions=$suppfile $cmd" if -r $suppfile;
    }

    {
	my $suppfile = $0;
	$suppfile =~ s/\.pl$/.supp/;
	$cmd = "--suppressions=$suppfile $cmd" if -r $suppfile;
    }

    # $cmd = "--show-reachable=yes $cmd";
    $cmd = "--show-below-main=yes $cmd";
    $cmd = "--leak-check=full $cmd";
    $cmd = "--num-callers=20 $cmd";
    $cmd = "--track-fds=yes $cmd";
    if ($valhelp =~ /--log-file-exactly=/) {
	$cmd = "--log-file-exactly=$outfile $cmd";
    } else {
	$cmd = "--log-file=$outfile $cmd";
    }
    $cmd = "valgrind $cmd";
    $cmd = "../libtool --mode=execute $cmd" if $uselibtool;

    my $code = system ($cmd);
    &system_failure ('valgrind', $code) if $code;

    my $txt = &read_file ($outfile);
    &removejunk ($outfile);
    my $errors = ($txt =~ /ERROR\s+SUMMARY:\s*(\d+)\s+errors?/i)
	? $1
	: -1;
    if ($errors == 0) {
	# &dump_indented ($txt);
	print STDERR "Pass\n";
	return;
    }

    &dump_indented ($txt);
    die "Fail\n";
}

# -----------------------------------------------------------------------------

sub test_ssindex {
    my ($file,$test) = @_;

    &report_skip ("file $file does not exist") unless -r $file;

    my $xmlfile = fileparse ($file);
    $xmlfile =~ s/\.[a-zA-Z0-9]+$/.xml/;
    unlink $xmlfile;
    die "Cannot remove $xmlfile.\n" if -f $xmlfile;
    &junkfile ($xmlfile);

    {
	my $cmd = "$ssindex --index '$file'";
	my $output = `$cmd 2>&1 >'$xmlfile'`;
	my $err = $?;
	&dump_indented ($output);
	die "Failed command: $cmd\n" if $err;
    }

    my $parser = new XML::Parser ('Style' => 'Tree');
    my $tree = $parser->parsefile ($xmlfile);
    &removejunk ($xmlfile);

    my @items;

    die "$0: Invalid parse tree from ssindex.\n"
	unless (ref ($tree) eq 'ARRAY' && $tree->[0] eq "gnumeric");
    my @children = @{$tree->[1]};
    my $attrs = shift @children;

    while (@children) {
	my $tag = shift @children;
	my $content = shift @children;

	if ($tag eq '0') {
	    # A text node
	    goto FAIL unless $content =~ /^\s*$/;
	} elsif ($tag eq 'data') {
	    my @dchildren = @$content;
	    my $dattrs = shift @dchildren;
	    die "$0: Unexpected attributes in data tag\n" if keys %$dattrs;
	    die "$0: Unexpected data tag content.\n" if @dchildren != 2;
	    die "$0: Unexpected data tag content.\n" if $dchildren[0] ne '0';
	    my $data = $dchildren[1];
	    push @items, $data;
	} else {
	    die "$0: Unexpected tag \"$tag\".\n";
	}
    }

    local $_ = \@items;
    if (&$test ($_)) {
	print STDERR "Pass\n";
    } else {
	die "Fail\n";
    }
}

# -----------------------------------------------------------------------------

sub report_skip {
    my ($txt) = @_;

    print "SKIP -- $txt\n";
    # 77 is magic for automake
    exit 77;
}

# -----------------------------------------------------------------------------
# Setup a consistent environment

&report_skip ("all tests skipped") if exists $ENV{'GNUMERIC_SKIP_TESTS'};

delete $ENV{'G_SLICE'};
$ENV{'G_DEBUG'} = 'fatal_criticals';

delete $ENV{'LANG'};
delete $ENV{'LANGUAGE'};
foreach (keys %ENV) { delete $ENV{$_} if /^LC_/; }
$ENV{'LC_ALL'} = 'C';

# libgsf listens for this
delete $ENV{'WINDOWS_LANGUAGE'};

if (@ARGV && $ARGV[0] eq '--verbose') {
    $verbose = 1;
    scalar shift @ARGV;
}

1;
