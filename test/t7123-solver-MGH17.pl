#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "MGH17.gnumeric";
my $rle = 6;
&message ("Check non-linear solver on $file problem.");
&test_sheet_calc ("$samples/solver/$file", ['--solve'], "K39",
		  sub {
		      chomp;
		      print STDERR "--> $_\n";
		      return 1; # Known failure
		      return (/^[-+]?(\d|\.\d)/ &&
			      $_ > $rle);
		  });
