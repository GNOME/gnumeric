#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "numbermatch.gnumeric";
&message ("Check that $file evaluates correctly.");
$ENV{'GNM_DEBUG'} = 'testsuite';
&test_sheet_calc ("$samples/$file", "B2", sub { /^0$/ });
