#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check separator guessing for csv files with quoted fields.");

# Note that the file extension is "csv".  That makes the importer guess the
# separator from the quoted fields, see stf_parse_options_guess_csv.  The
# first line is left unquoted as it is not used for guessing unless it is
# the only line.

my @layouts = (
    # Quote at start of line, separator right after
    sub { my $sep = shift; "\"abc\"${sep}\"def\"${sep}\"ghi\"\n"; },
    # Separator before the quote
    sub { my $sep = shift; "abc${sep}\"def\"${sep}ghi\n"; },
    # Blank between quote and separator
    sub { my $sep = shift; "\"abc\" ${sep}\"def\" ${sep}\"ghi\"\n"; },
);

for my $sep (',', ';', ':', '|', "\t") {
    for my $layout (@layouts) {
	my $tmp1 = &GnumericTest::invent_junkfile ("foo.csv");
	my $tmp2 = &GnumericTest::invent_junkfile ("foo.txt");

	my $row = &$layout ($sep);
	&GnumericTest::write_file ($tmp1,
				   "h1${sep}h2${sep}h3\n" . $row . $row);

	my @cmd = ($ssconvert, $tmp1, $tmp2);
	print "# ", &GnumericTest::quotearg (@cmd), "\n" if $GnumericTest::verbose;
	my $code = system (@cmd);
	&system_failure ($ssconvert, $code) if $code;
	my $out = &GnumericTest::read_file ($tmp2);

	my $expected = "h1${sep}h2${sep}h3\n" .
	    "abc${sep}def${sep}ghi\n" .
	    "abc${sep}def${sep}ghi\n";

	if (($out // '') ne $expected) {
	    print "Separator: ", ($sep eq "\t" ? '\t' : $sep), "\n";
	    print "Input:\n";
	    &GnumericTest::dump_indented (&GnumericTest::read_file ($tmp1));
	    print "Output:\n";
	    &GnumericTest::dump_indented ($out);
	    die "Fail\n";
	}

	&GnumericTest::removejunk ($tmp2);
	&GnumericTest::removejunk ($tmp1);
    }
}

print "Pass\n";
