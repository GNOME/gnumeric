#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&test_sheet_calc ("$samples/excel/bitwise.xls", "B3:B99", sub { /^(\s*Success)+\s*$/i });
