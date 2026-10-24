#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

#
# Known failure.
#
# The sample allows 1000 iterations and the solver is still some way off by
# then.  With 100000 iterations it converges (in about 29000 iterations) to a
# residual sum of squares of 1.6118e-8, against 1.6117e-8 certified, but the
# parameters are only good to about 2.4 digits.  It then stops for good: the
# problem has a narrow, curved valley (scaled Hessian condition number around
# 6e8) and the search cannot follow it any further.  A Levenberg-Marquardt
# polish from that point reaches the certified parameters.
#
# Note also that a search that only sees the residual sum of squares can at
# best resolve something like 5.7 digits here because of rounding noise in
# the sum, which is below the 6 digits required below.
#
# The model is symmetric under permuting the three (amplitude, rate) pairs,
# so the K column of the sample file compares the pairs sorted by rate.
#

my $file = "Lanczos3.gnumeric";
my $rle = 6;
&message ("Check non-linear solver on $file problem.");
&test_sheet_calc ("$samples/solver/$file", ['--solve'], "K39",
		  sub {
		      chomp;
		      print STDERR "--> $_\n";
		      return (/^[-+]?(\d|\.\d)/ &&
			      $_ > $rle);
		  });
