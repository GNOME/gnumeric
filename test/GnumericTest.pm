package GnumericTest;
use strict;
use Exporter;
use File::Basename qw(fileparse);
use Config;

@GnumericTest::ISA = qw (Exporter);
@GnumericTest::EXPORT = qw(test_sheet_calc test_importer test_valgrind
			   test_command
			   $ssconvert $topsrc $samples $PERL);
@GnumericTest::EXPORT_OK = qw(junkfile);

use vars qw($topsrc $samples $ssconvert $PERL);
$topsrc = "..";
$samples = "$topsrc/samples";
$ssconvert = "$topsrc/src/ssconvert";
$PERL = $Config{'perlpath'};
$PERL .= $Config{'_exe'} if $^O ne 'VMS' && $PERL !~ m/$Config{'_exe'}$/i;

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

sub dump_indented {
    my ($txt) = @_;
    my @lines = split (/\n/, $txt);
    print STDERR "| ", join ("\n| ", @lines), "\n";
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

sub test_sheet_calc {
    my ($file,$range,$expected) = @_;

    my $tmp = fileparse ($file);
    $tmp =~ s/\.[a-zA-Z0-9]+$/.csv/;
    &junkfile ($tmp);

    my $code = system ("$ssconvert --recalc --export-range='$range' '$file' '$tmp' 2>&1 | sed -e 's/^/| /'");
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
	die "Fail.\n$actual\n";
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

    my $code = system ("$ssconvert '$file' '$tmp' 2>&1 | sed -e 's/^/| /'");
    &system_failure ($ssconvert, $code) if $code;

    my $htxt = `gzip -dc '$tmp' | grep -v '^ *<gnm:Version .*>' | sha1sum`;
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
	my $code1 = system ("gzip -dc '$saved' >'$tmp1'");
	&system_failure ('gzip', $code1) if $code1;

	my $tmp2 = "$tmp-new";
	&junkfile ($tmp2);
	my $code2 = system ("gzip -dc '$tmp' >'$tmp2'");
	&system_failure ('gzip', $code2) if $code2;

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

my $valgrind_version;

sub test_valgrind {
    my ($cmd,$uselibtool) = @_;

    local %ENV;
    $ENV{'G_DEBUG'} = 'gc-friendly';
    $ENV{'G_SLICE'} = 'always-malloc';
    delete $ENV{'VALGRIND_OPTS'};

    if (!defined $valgrind_version) {
	my $vtxt = `valgrind --version 2>&1`;
	if ($vtxt =~ /((\d+\.)+\d+)$/) {
	    $valgrind_version = $1;
	} else {
	    die "Cannot determine valgrind version.\n";
	}
    }

    my $outfile = 'valgrind.log';
    unlink $outfile;
    die "Cannot remove $outfile.\n" if -f $outfile;
    &junkfile ($outfile);

    my $suppfile = $0;
    $suppfile =~ s/\.pl$/.supp/;

    # $cmd = "--gen-suppressions=all $cmd";
    $cmd = "--suppressions=$suppfile $cmd" if -r $suppfile;
    # $cmd = "--show-reachable=yes $cmd";
    $cmd = "--leak-check=full $cmd";
    $cmd = "--num-callers=20 $cmd";
    $cmd = "--error-exitcode=1 $cmd" if $valgrind_version ge "3.2.1";
    $cmd = "--track-fds=yes $cmd";
    $cmd = "--log-file-exactly=$outfile $cmd";
    $cmd = "valgrind $cmd";
    $cmd = "../libtool --mode=execute $cmd" if $uselibtool;

    my $code = system ($cmd);
    &system_failure ('valgrind', $code) if $code;

    my $txt = &read_file ($outfile);
    &removejunk ($outfile);
    my $errors = ($txt =~ /ERROR SUMMARY: (\d+) errors? from/i)
	? $1
	: -1;
    if ($errors == 0) {
	print STDERR "Pass\n";
	return;
    }

    &dump_indented ($txt);
    die "Fail\n";
}

# -----------------------------------------------------------------------------
# Setup a consistent environment

delete $ENV{'LANG'};
foreach (keys %ENV) { delete $ENV{$_} if /^LC_/; }
$ENV{'LC_ALL'} = 'C';

1;
