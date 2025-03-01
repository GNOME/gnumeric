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
                           test_csv_format_guessing
			   test_ssindex sstest test_command message subtest
                           test_tool
                           setup_python_environment
                           make_absolute
			   $ssconvert $sstest $ssdiff $ssgrep $gnumeric
                           $topsrc $top_builddir
			   $subtests $samples corpus $PERL $PYTHON);
@GnumericTest::EXPORT_OK = qw(junkfile);

use vars qw($topsrc $top_builddir $samples $default_subtests $default_corpus $PERL $PYTHON $verbose);
use vars qw($ssconvert $ssindex $sstest $ssdiff $ssgrep $gnumeric);
use vars qw($normalize_gnumeric);

$PYTHON = undef;

$PERL = $Config{'perlpath'};
$PERL .= $Config{'_exe'} if $^O ne 'VMS' && $PERL !~ m/$Config{'_exe'}$/i;

if ($0 eq '-e') {
    # Running as "perl -e '...'", so no idea about where we are
    $topsrc = '.';
} else {
    $topsrc = $0;
    $topsrc =~ s|/[^/]+$|/..|;
    $topsrc =~ s|/test/\.\.$||;
}

$top_builddir = "..";
$samples = "$topsrc/samples"; $samples =~ s{^\./+}{};
$ssconvert = "$top_builddir/src/ssconvert";
$ssindex = "$top_builddir/src/ssindex";
$sstest = "$top_builddir/src/sstest";
$ssdiff = "$top_builddir/src/ssdiff";
$ssgrep = "$top_builddir/src/ssgrep";
$gnumeric = "$top_builddir/src/gnumeric";
$normalize_gnumeric = "$topsrc/test/normalize-gnumeric";
$verbose = 0;
$default_subtests = '*';
my $subtests = undef;
$default_corpus = 'full';
my $user_corpus = undef;

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
    my ($p, $nofail) = @_;

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

    return undef if $nofail;

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

sub subtest {
    my ($q) = @_;

    my $res = 0;
    foreach my $t (split (',', $subtests || $default_subtests)) {
	if ($t eq '*' || $t eq $q) {
	    $res = 1;
	    next;
	} elsif ($t eq '-*' || $t eq "-$q") {
	    $res = 0;
	    next;
	}
    }
    return $res;
}

# -----------------------------------------------------------------------------

my @dist_corpus =
    ("$samples/regress.gnumeric",
     "$samples/excel/address.xls",
     "$samples/excel/bitwise.xls",
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
     "$samples/excel12/database.xlsx",
     "$samples/excel12/ifs-funcs.xlsx",
     "$samples/excel12/countif.xlsx",
     "$samples/crlibm.gnumeric",
     "$samples/amath.gnumeric",
     "$samples/gamma.gnumeric",
     "$samples/linest.xls",
     "$samples/vba-725220.xls",
     "$samples/sumif.xls",
     "$samples/array-intersection.xls",
     "$samples/arrays.xls",
     "$samples/docs-samples.gnumeric",
     "$samples/ftest.xls",
     "$samples/ttest.xls",
     "$samples/chitest.xls",
     "$samples/vdb.gnumeric",
     "$samples/cronbach.gnumeric",
     "$samples/ilog.gnumeric",
     "$samples/numbermatch.gnumeric",
     "$samples/numtheory.gnumeric",
     "$samples/solver/afiro.mps",
     "$samples/solver/blend.mps",
     "$samples/auto-filter-tests.gnumeric",
     "$samples/cell-comment-tests.gnumeric",
     "$samples/colrow-tests.gnumeric",
     "$samples/cond-format-tests.gnumeric",
     "$samples/format-tests.gnumeric",
     "$samples/formula-tests.gnumeric",
     "$samples/graph-tests.gnumeric",
     "$samples/hlink-tests.gnumeric",
     "$samples/intersection-tests.gnumeric",
     "$samples/merge-tests.gnumeric",
     "$samples/names-tests.gnumeric",
     "$samples/number-tests.gnumeric",
     "$samples/object-tests.gnumeric",
     "$samples/page-setup-tests.gnumeric",
     "$samples/rich-text-tests.gnumeric",
     "$samples/sheet-formatting-tests.gnumeric",
     "$samples/sheet-names-tests.gnumeric",
     "$samples/sheet-tab-tests.gnumeric",
     "$samples/solver-tests.gnumeric",
     "$samples/split-panes-tests.gnumeric",
     "$samples/string-tests.gnumeric",
     "$samples/merge-tests.gnumeric",
     "$samples/selection-tests.gnumeric",
     "$samples/style-tests.gnumeric",
     "$samples/validation-tests.gnumeric",
     "$samples/recalc725.gnumeric",
    );

my @full_corpus =
    ("$samples/excel/chart-tests-excel.xls",   # Too big
     @dist_corpus);


sub corpus {
    my ($c) = @_;

    my $corpus = ($c // $user_corpus // $default_corpus);
    if ($corpus eq 'full') {
	return @full_corpus;
    } elsif ($corpus eq 'dist') {
	return @dist_corpus;
    } elsif ($corpus =~ /^random:(\d+)$/) {
	my $n = $1;
	my @corpus = grep { -r $_; } @full_corpus;
	while ($n < @corpus) {
	    my $i = int (rand() * @corpus);
	    splice @corpus, $i, 1;
	}
	return @corpus;
    } elsif ($corpus =~ m{^/(.*)/$}) {
	my $rx = $1;
	my @corpus = grep { /$rx/ } @full_corpus;
	return @corpus;
    } else {
	die "Invalid corpus specification\n";
    }
}

# -----------------------------------------------------------------------------

sub test_command {
    my ($cmd,$test) = @_;

    print STDERR "# $cmd\n" if $verbose;
    my $output = `$cmd 2>&1`;
    my $err = $?;
    &dump_indented ($output);
    die "Failed command: $cmd\n" if $err;

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

    my $cmd = &quotearg ($sstest, $test);
    print STDERR "# $cmd\n" if $verbose;
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

    my $cmd = "$ssconvert " . &quotearg (@$pargs, '--recalc', "--export-range=$range", $file, $tmp);
    print STDERR "# $cmd\n" if $verbose;
    my $code = system ("$cmd 2>&1 | sed -e 's/^/| /' ");
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
    my ($file,$sha1,$args) = @_;

    my $mode = $args->{'mode'};

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

    my @normalize = ($PERL, $normalize_gnumeric);
    push @normalize, '--ignore-default-size' if $args->{'nofont'};
    my $norm = &quotearg (@normalize);

    my $htxt = `zcat -f '$tmp' | $norm | sha1sum`;
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
    my ($file,$ext) = @_;

    &report_skip ("file $file does not exist") unless -r $file;

    my $tmp = fileparse ($file);
    $tmp =~ s/\.([a-zA-Z0-9]+)$//;
    $ext = $1 unless defined $ext;
    $ext or die "Must have extension for export test.";
    my $code;
    my $keep = 0;

    my $tmp1 = "$tmp.gnumeric";
    &junkfile ($tmp1) unless $keep;
    {
	my $cmd = &quotearg ($ssconvert, $file, $tmp1);
	print STDERR "# $cmd\n" if $verbose;
	my $code = system ("$cmd 2>&1 | sed -e 's/^/| /'");
	&system_failure ($ssconvert, $code) if $code;
    }

    my $tmp2 = "$tmp-new.$ext";
    &junkfile ($tmp2) unless $keep;
    {
	my $cmd = &quotearg ($ssconvert, $file, $tmp2);
	print STDERR "# $cmd\n" if $verbose;
	my $code = system ("$cmd 2>&1 | sed -e 's/^/| /'");
	&system_failure ($ssconvert, $code) if $code;
    }

    my $tmp3 = "$tmp-new.gnumeric";
    &junkfile ($tmp3) unless $keep;
    {
	my $cmd = &quotearg ($ssconvert, $tmp2, $tmp3);
	print STDERR "# $cmd\n" if $verbose;
	my $code = system ("$cmd 2>&1 | sed -e 's/^/| /'");
	&system_failure ($ssconvert, $code) if $code;
    }

    my $tmp4 = "$tmp.xml";
    &junkfile ($tmp4) unless $keep;
    $code = system (&quotearg ("zcat", "-f", $tmp1) . "| $PERL $normalize_gnumeric >" . &quotearg ($tmp4));
    &system_failure ('zcat', $code) if $code;

    my $tmp5 = "$tmp-new.xml";
    &junkfile ($tmp5) unless $keep;
    $code = system (&quotearg ("zcat" , "-f", $tmp3) . " | $PERL $normalize_gnumeric >" . &quotearg ($tmp5));
    &system_failure ('zcat', $code) if $code;

    $code = system ('diff', '-u', $tmp4, $tmp5);
    &system_failure ('diff', $code) if $code;

    print STDERR "Pass\n";
}

# -----------------------------------------------------------------------------

sub test_csv_format_guessing {
    my (%args) = @_;
    my $data = $args{'data'};

    my $keep = 0;

    my $datafn = "test-data.csv";
    &junkfile ($datafn) unless $keep;
    &write_file ($datafn, $data);

    my $outfn = "test-data.gnumeric";
    &junkfile ($outfn) unless $keep;

    local $ENV{'GNM_DEBUG'} = 'stf';
    my $cmd = &quotearg ($ssconvert, $datafn, $outfn);
    print STDERR "# $cmd\n" if $verbose;
    my $out = `$cmd 2>&1`;

    if ($out !~ m/^\s*fmt\.0\s*=\s*(\S+)\s*$/m) {
	die "Failed to guess any format\n";
    }
    my $guessed = $1;

    my $ok;
    {
	local $_ = $guessed;
	$ok = &{$args{'format'}} ($_);
    }

    if ($verbose || !$ok) {
	print STDERR "Data:\n";
	foreach (split ("\n", $data)) {
	    print STDERR "| $_\n";
	}
	print STDERR "Result:\n";
	foreach (split ("\n", $out)) {
	    print STDERR "| $_\n";
	}
    }

    die "Guessed wrong format: $guessed\n" unless $ok;

    if (exists $args{'decimal'}) {
	if ($out !~ m/^\s*fmt\.0\.dec\s*=\s*(\S+)\s*$/m) {
	    die "Failed to guess any decimal separator\n";
	}
	my $guessed = $1;
	my $ok = ($1 eq $args{'decimal'});

	die "Guessed wrong decimal separator: $guessed\n" unless $ok;
    }

    if (exists $args{'thousand'}) {
	if ($out !~ m/^\s*fmt\.0\.thou\s*=\s*(\S+)\s*$/m) {
	    die "Failed to guess any thousands separator\n";
	}
	my $guessed = $1;
	my $ok = ($1 eq $args{'thousand'});

	die "Guessed wrong thousands separator: $guessed\n" unless $ok;
    }

    &removejunk ($outfn) unless $keep;
    &removejunk ($datafn) unless $keep;
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

my $noframe_filter = "$PERL -p -e '\$_ = \"\" if m{<gnm:SheetWidgetFrame .*/>}'";

my $noasindex_filter = "$PERL -p -e 'if (/gnm:SheetWidget(List|Combo)/) { s{( OutputAsIndex=)\"\\d+\"}{\$1\"0\"}; }'";

my $ods_strict_filter =
    "$PERL -p -e 's/(placement=\")\\w+(\")/\$1XXX\$2/ if m{<gnm:comments}'";

sub normalize_filter {
    my ($f) = @_;
    return 'cat' unless defined $f;

    $f =~ s/\bstd:drop_codepage\b/$drop_codepage_filter/;
    $f =~ s/\bstd:drop_generator\b/$drop_generator_filter/;
    $f =~ s/\bstd:no_author\b/$no_author_filter/;
    $f =~ s/\bstd:no_rich_comment\b/$no_rich_comment_filter/;
    $f =~ s/\bstd:supersub\b/$supersub_filter/;
    $f =~ s/\bstd:noframewidget\b/$noframe_filter/;
    $f =~ s/\bstd:nocomboasindex\b/$noasindex_filter/;
    $f =~ s/\bstd:ods_strict\b/$ods_strict_filter/;

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

    my $filter0 = &normalize_filter ($named_args{'filter0'});
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
	my $cmd = &quotearg ($ssconvert, "--resize", $resize, $file, $file_resized);
	print STDERR "# $cmd\n" if $verbose;
	$code = system ("$cmd 2>&1 | sed -e 's/^/| /'");
	&system_failure ($ssconvert, $code) if $code;
	die "Failed to produce $file_resized\n" unless -r $file_resized;
	&junkfile ($file_resized) unless $keep;
    }

    my $file_filtered = $file_resized;
    if ($filter0) {
	$file_filtered =~ s{^.*/}{};
	$file_filtered =~ s/(\.gnumeric)$/-filter$1/;
	unlink $file_filtered;
	my $cmd = "zcat " . &quotearg ($file_resized) . " | $filter0 >" . &quotearg ($file_filtered);
	print STDERR "# $cmd\n" if $verbose;
	$code = system ("($cmd) 2>&1 | sed -e 's/^/| /'");
	&system_failure ($ssconvert, $code) if $code;
	die "Failed to produce $file_filtered\n" unless -r $file_filtered;
	&junkfile ($file_filtered) unless $keep;
    }

    my $tmp1 = "$tmp.$newext";
    unlink $tmp1;
    &junkfile ($tmp1) unless $keep;
    {
	my $cmd = &quotearg ($ssconvert, "-T", $format, $file_filtered, $tmp1);
	print "# $cmd\n" if $verbose;
	my $code = system ("$cmd 2>&1 | sed -e 's/^/| /'");
	&system_failure ($ssconvert, $code) if $code;
	die "Failed to produce $tmp1\n" unless -r $tmp1;
    }

    my $tmp2 = "$tmp-new.$ext";
    unlink $tmp2;
    &junkfile ($tmp2) unless $keep;
    {
	my $cmd = &quotearg ($ssconvert, $tmp1, $tmp2);
	print "# $cmd\n" if $verbose;
	my $code = system ("$cmd 2>&1 | sed -e 's/^/| /'");
	&system_failure ($ssconvert, $code) if $code;
	die "Failed to produce $tmp2\n" unless -r $tmp2;
    }

    my $tmp_xml = "$tmp.xml";
    unlink $tmp_xml;
    &junkfile ($tmp_xml) unless $keep;
    $code = system ("zcat -f '$file_filtered' | $PERL $normalize_gnumeric | $filter1 >'$tmp_xml'");
    &system_failure ('zcat', $code) if $code;

    my $tmp2_xml = "$tmp-new.xml";
    unlink $tmp2_xml;
    &junkfile ($tmp2_xml) unless $keep;
    # print STDERR "zcat -f '$tmp2' | $PERL $normalize_gnumeric | $filter2 >'$tmp2_xml'\n";
    $code = system ("zcat -f '$tmp2' | $PERL $normalize_gnumeric | $filter2 >'$tmp2_xml'");
    &system_failure ('zcat', $code) if $code;

    $code = system ('diff', '-u', $tmp_xml, $tmp2_xml);
    &system_failure ('diff', $code) if $code && !$ignore_failure;

    print STDERR "Pass\n";
}

# -----------------------------------------------------------------------------

sub test_valgrind {
    my ($cmd,$uselibtool,$qreturn) = @_;

    local (%ENV) = %ENV;
    $ENV{'G_DEBUG'} .= ':gc-friendly:resident-modules';
    $ENV{'G_SLICE'} = 'always-malloc';

    # Turn off python's crazy malloc that likes to read memory it does
    # not own.  That will trigger valgrind complaints (or spurious
    # breakpoints in gdb, for example).
    $ENV{'PYTHONMALLOC'} = 'malloc';

    # Turn off avx versions of libc functions.  Those have a bad habit
    # of reading beyond the end of the string in a way that tickles
    # valgrind
    $ENV{'GLIBC_TUNABLES'} = 'glibc.cpu.hwcaps=-AVX2,-AVX';

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
    $cmd = "--enable-debuginfod=yes $cmd";

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
	print STDERR "Pass\n" unless $qreturn;
	return 0;
    }

    &dump_indented ($txt);
    die "Fail\n" unless $qreturn;
    return 1;
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
	my $cmd = &quotearg ($ssindex, "--index", $file);
	print STDERR "# $cmd\n" if $verbose;
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
      FAIL:
	die "Fail\n";
    }
}

# -----------------------------------------------------------------------------

sub test_tool {
    my ($file,$tool,$tool_args,$range,$test) = @_;

    &report_skip ("file $file does not exist") unless -r $file;

    my @args;
    push @args, "--export-range=$range" if defined $range;
    push @args, "--tool-test=$tool";
    for (my $i = 0; $i + 1 < @$tool_args; $i += 2) {
	my $k = $tool_args->[$i];
	my $v = $tool_args->[$i + 1];
	push @args, "--tool-test=$k:$v";
    }

    my $tmp = "tool.csv";
    &junkfile ($tmp);

    my $cmd = &quotearg ($ssconvert, @args, $file, $tmp);
    print STDERR "# $cmd\n" if $GnumericTest::verbose;
    my $code = system ($cmd);
    &system_failure ($ssconvert, $code) if $code;
    my $actual = &read_file ($tmp);

    &removejunk ($tmp);

    if (&$test ($actual)) {
	print STDERR "Pass\n";
    } else {
	&GnumericTest::dump_indented ($actual);
	die "Fail\n";
    }
}

# -----------------------------------------------------------------------------

sub has_linear_solver {
    return (defined (&find_program ('lp_solve', 1)) ||
	    defined (&find_program ('glpsol', 1)));
}

# -----------------------------------------------------------------------------

sub make_absolute {
    my ($fn) = @_;

    return $fn if $fn =~ m{^/};
    $fn =~ s{^\./+([^/])}{$1};
    my $pwd = $ENV{'PWD'};
    $pwd .= '/' unless $pwd =~ m{/$};
    return "$pwd$fn";
}

# -----------------------------------------------------------------------------

sub setup_python_environment {
    $PYTHON = `grep '^#define PYTHON_INTERPRETER ' $top_builddir/gnumeric-config.h 2>&1`;
    chomp $PYTHON;
    $PYTHON =~ s/^[^"]*"(.*)"\s*$/$1/;
    &report_skip ("Missing python interpreter") unless -x $PYTHON;

    # Make sure we load introspection preferentially from build directory
    my $v = 'GI_TYPELIB_PATH';
    my $dir = "$top_builddir/src";
    $ENV{$v} = ($ENV{$v} || '') eq '' ? $dir : $dir . ':' . $ENV{$v};

    # Ditto for shared libraries
    $v = 'LD_LIBRARY_PATH';
    $dir = "$top_builddir/src/.libs";
    $ENV{$v} = ($ENV{$v} || '') eq '' ? $dir : $dir . ':' . $ENV{$v};

    $ENV{'GNM_TEST_INTROSPECTION_DIR'} = &make_absolute ("$topsrc/introspection/gi/overrides");

    # Don't litter
    $ENV{'PYTHONDONTWRITEBYTECODE'} = 1;

    $0 = &make_absolute ($0);
    $ENV{'GNM_TEST_TOP_BUILDDIR'} = $top_builddir;
}

# -----------------------------------------------------------------------------

sub quotearg {
    return join (' ', map { &quotearg1 ($_) } @_);
}

sub quotearg1 {
    my ($arg) = @_;

    return "''" if $arg eq '';
    my $res = '';
    while ($arg ne '') {
	if ($arg =~ m!^([-=/._a-zA-Z0-9:]+)!) {
	    $res .= $1;
	    $arg = substr ($arg, length $1);
	} else {
	    $res .= "\\" . substr ($arg, 0, 1);
	    $arg = substr ($arg, 1);
	}
    }
    return $res;
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

my $seed = time();

while (1) {
    if (@ARGV && $ARGV[0] eq '--verbose') {
	$verbose = 1;
	scalar shift @ARGV;
	next;
    } elsif (@ARGV > 1 && $ARGV[0] eq '--subtests') {
	scalar shift @ARGV;
	$subtests = shift @ARGV;
    } elsif (@ARGV > 1 && $ARGV[0] eq '--corpus') {
	scalar shift @ARGV;
	$user_corpus = shift @ARGV;
    } else {
	last;
    }
}

srand ($seed);

1;
