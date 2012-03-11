#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check the xls exporter.");
&test_exporter ("$samples/excel/mathfuns.xls");
