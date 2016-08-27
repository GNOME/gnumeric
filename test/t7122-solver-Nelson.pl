#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "Nelson.gnumeric";
my $rle = 4.7;
&message ("Check non-linear solver on $file problem.");
&test_sheet_calc ("$samples/solver/$file", ['--solve'], "K39",
		  sub {
		      chomp;
		      print STDERR "--> $_\n";
		      my $ok = (/^[-+]?(\d|\.\d)/ && $_ > $rle);
		      return $ok;
		  });
