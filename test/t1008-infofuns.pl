#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&test_sheet_calc ("$samples/excel/infofuns.xls", "A4", sub { /All ok/i });
