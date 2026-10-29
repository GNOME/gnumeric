#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "partitions.gnumeric";
&message ("Check that $file evaluates correctly.");
&test_sheet_calc ("$samples/$file", "B1", sub { /^Ok$/i });
