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
# minimum is 2.2e-11.  This is a real local minimum and not a stall: a
# Levenberg-Marquardt polish from there returns the same point, also after
# nudging it.  Nelder-Mead finds the global minimum from this starting point
# but ends up in the same local minimum from the second one.
#
# From the second starting point the solver does reach the certified minimum
# (about 5.7 digits, in about 2000 iterations).  With a scaled Hessian
# condition number around 6e8, a search that only sees the residual sum of
# squares can resolve roughly 6.5 digits at best.
#
# The model is symmetric under permuting the three (amplitude, rate) pairs,
# so the K column of the sample file compares the pairs sorted by rate.
#

my $file = "Lanczos2.gnumeric";
my $rle = 6;
&message ("Check non-linear solver on $file problem.");
&test_sheet_calc ("$samples/solver/$file", ['--solve'], "K39",
		  sub {
		      print STDERR "--> $_\n";
		      return (/^[-+]?(\d|\.\d)/ &&
			      $_ > $rle);
		  });
