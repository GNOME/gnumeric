#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $src = "$samples/formats.xls";
&GnumericTest::report_skip ("file $src does not exist") unless -r $src;

# Text formats
for (['csv', 'Gnumeric_stf:stf_csv'],
     ['txt', 'Gnumeric_stf:stf_assistant']) {
    my ($fmt,$exporter) = @$_;

    next unless &subtest ($fmt);
    &message ("Checking ssconvert sheet selection for $fmt");

    my $cmd = "$ssconvert -O 'sheet=General' -T $exporter $src fd://1";
    print STDERR "# $cmd\n" if $GnumericTest::verbose;
    my $out = `$cmd 2>&1`;
    my $err = $?;
    if ($err) {
	&dump_indented ($out);
	die "Failed command: $cmd\n";
    }

    if ($out =~ /Generalxx/ && $out !~ /Goffice configuration/) {
	print STDERR "Pass\n";
    } else {
	die "Fail\n";
    }
}


# Ought to check print formats here
