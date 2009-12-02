#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "blend.mps";
my $answer = -30.81221619004;
&message ("Check solver on $file problem.");
&test_sheet_calc ("$samples/solver/$file", ['--solve'], "B5",
		  sub {
		      chomp;
		      return (/^[-+]?(\d|\.\d)/ &&
			      abs ($answer - $_) < 1e-6);
		  });
