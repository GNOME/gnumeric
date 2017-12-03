#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check the sylk exporter.");
&test_exporter ("$samples/sylk/app_b2.sylk", "slk");
