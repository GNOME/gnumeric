#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check ssconvert merge");

my $src1 = "$samples/regress.gnumeric";
my $src2 = "$samples/format-tests.gnumeric";

my $tmp = "merged.gnumeric";
&GnumericTest::junkfile ($tmp);

# Not much of a test, but this at least confirms that the program
# runs with no criticals.
&test_command ("$ssconvert --merge-to=$tmp $src1 $src2",
	       sub { 1 } );
