#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "gsl.gnumeric";
&message ("Check that $file evaluates correctly.");
&test_sheet_calc ("$samples/$file", "C2:F2",
		  sub {
		      foreach my $a (split (',')) {
			  return 0 unless $a > 15;
		      }
		      return 1;
		  });
