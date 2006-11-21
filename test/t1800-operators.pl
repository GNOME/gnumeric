#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&test_sheet_calc ("$samples/excel/operator.xls", "C1:C15", sub { /^(\s*Success)+\s*$/i });
