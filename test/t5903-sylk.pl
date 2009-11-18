#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sylk importer.");
&test_importer ("$samples/sylk/test.sylk", "563a755d6238f2eb9d41f724bb21e56974067453", $mode);
&test_importer ("$samples/sylk/encoding.sylk", "f8f29c64305dda6313e96588dbd18a7cc1740f1d", $mode);
&test_importer ("$samples/sylk/app_b2.sylk", "4b5101407d9b7106db1eff95d92a0f42237948b2", $mode);
