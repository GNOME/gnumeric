#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check the csv importer.");

for my $term ("\n", "\r\n", "\r") {
    my $tmp1 = &GnumericTest::invent_junkfile ("foo.cvs");
    my $tmp2 = &GnumericTest::invent_junkfile ("foo.txt");

    &GnumericTest::write_file ($tmp1, "1,2,3${term}2,3,4${term}3,4,5${term}");

    my @cmd = ($ssconvert, $tmp1, $tmp2);
    print "# ", &GnumericTest::quotearg (@cmd), "\n" if $GnumericTest::verbose;
    my $code = system (@cmd);
    &system_failure ($ssconvert, $code) if $code;
    my $out = &GnumericTest::read_file ($tmp2);

    my $expected = "1,2,3${term}2,3,4${term}3,4,5${term}";

    if (($out // '') ne $expected) {
	&GnumericTest::dump_indented ($out);
	die "Fail\n";
    }

    &GnumericTest::removejunk ($tmp2);
    &GnumericTest::removejunk ($tmp1);
}

print "Pass\n";
