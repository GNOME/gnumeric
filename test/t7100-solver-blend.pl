#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&GnumericTest::report_skip ("No linear solver found")
    unless &GnumericTest::has_linear_solver ();

my $file = "blend.mps";
my $answer = -30.8121498458281;
# lp_solve:  -30.81221619004;
my $tol = 1e-4;
&message ("Check solver on $file problem.");
&test_sheet_calc ("$samples/solver/$file", ['--solve'], "B5",
		  sub {
		      chomp;
		      return (/^[-+]?(\d|\.\d)/ &&
			      abs ($answer - $_) < $tol);
		  });
