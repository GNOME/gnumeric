#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sc importer.");
&test_importer ("$samples/sc/demo_func", "f75a74d61d060dabd6b83f88be3f43287cef352c", $mode);
&test_importer ("$samples/sc/demo_math", "7b73d488f46d13ee1e6eaf8eb2fdaf094e8f3ac1", $mode);
