#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sylk importer.");
&test_importer ("$samples/sylk/test.sylk", "f508b9a13f48f46fb01250acb0f9ddbc94ad918f", $mode);
&test_importer ("$samples/sylk/encoding.sylk", "bb9b8c232401e9993766bb995216b998b50fd6d5", $mode);
&test_importer ("$samples/sylk/app_b2.sylk", "3b559baea7373de85f9d6d4c633e46b052bd8598", $mode);
