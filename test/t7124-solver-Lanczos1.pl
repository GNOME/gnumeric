#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

#
# Known failure.
#
# From the first starting point the solver stops, after about 500 iterations,
# in a local minimum where b4 and b6 have merged (two nearly identical
# exponentials) with a residual sum of squares of about 4.3e-6; the certified
# minimum is 1.4e-25.  This is a real local minimum and not a stall: a
# Levenberg-Marquardt polish from there returns the same point, also after
# nudging it.  Derivative-free methods (Nelder-Mead, Powell) end up there too
# from the second starting point.  Only methods that use the sum-of-squares
# structure (LM, trust-region least squares) reach the global minimum.
#
# With a scaled Hessian condition number around 6e8, a search that only sees
# the residual sum of squares can at best resolve roughly 9 digits.
#
# The model is symmetric under permuting the three (amplitude, rate) pairs,
# so the K column of the sample file compares the pairs sorted by rate.
#

my $file = "Lanczos1.gnumeric";
my $rle = 6;
&message ("Check non-linear solver on $file problem.");
&test_sheet_calc ("$samples/solver/$file", ['--solve'], "K39",
		  sub {
		      chomp;
		      print STDERR "--> $_\n";
		      return (/^[-+]?(\d|\.\d)/ &&
			      $_ > $rle);
		  });
