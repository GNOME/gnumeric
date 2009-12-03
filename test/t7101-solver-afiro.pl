#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "afiro.mps";
my $answer = -464.753216;
# lp_solve:  -464.753216;
my $tol = 1e-4;
&message ("Check solver on $file problem.");
&test_sheet_calc ("$samples/solver/$file", ['--solve'], "B5",
		  sub {
		      chomp;
		      return (/^[-+]?(\d|\.\d)/ &&
			      abs ($answer - $_) < $tol);
		  });
