#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "gamma.gnumeric";
&message ("Check that $file evaluates correctly.");
&test_sheet_calc ("$samples/$file", "Overview!B1", sub { /^[0-9.]+$/ && $_ > 11.4; });
