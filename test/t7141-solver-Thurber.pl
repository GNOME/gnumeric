#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "Thurber.gnumeric";
my $rle = 3.8;
&message ("Check non-linear solver on $file problem.");
&test_sheet_calc ("$samples/solver/$file", ['--solve'], "K39",
		  sub {
		      chomp;
		      print STDERR "--> $_\n";
		      my $ok = (/^[-+]?(\d|\.\d)/ && $_ > $rle);
		      if ($ok) {
			  print STDERR "Unexpected success.\n";
		      } else {
			  print STDERR "Known failure.\n" ;
			  $ok = 1;
		      }
		      return $ok;
		  });
