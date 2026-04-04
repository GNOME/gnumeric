#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check the csv importer.");

for my $sep (',', ';', ':') {
    my $tmp1 = &GnumericTest::invent_junkfile ("foo.cvs");
    my $tmp2 = &GnumericTest::invent_junkfile ("foo.txt");

    &GnumericTest::write_file ($tmp1, <<DATA);
1.00${sep}2.0${sep}3.0
2e1${sep}3e1${sep}4e2
3e-1${sep}4e-1${sep}5e-2
DATA

    my @cmd = ($ssconvert, $tmp1, $tmp2);
    print "# ", &GnumericTest::quotearg (@cmd), "\n" if $GnumericTest::verbose;
    my $code = system (@cmd);
    &system_failure ($ssconvert, $code) if $code;
    my $out = &GnumericTest::read_file ($tmp2);

    my $expected = <<EXPECTED;
1${sep}2${sep}3
20${sep}30${sep}400
0.3${sep}0.4${sep}0.05
EXPECTED

    if (($out // '') ne $expected) {
	&GnumericTest::dump_indented ($out);
	die "Fail\n";
    }

    &GnumericTest::removejunk ($tmp2);
    &GnumericTest::removejunk ($tmp1);
}

print "Pass\n";
