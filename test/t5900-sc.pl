#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sc importer.");
&test_importer ("$samples/sc/demo_func", "39986c5c21cf60988221a6fa24ab482ee1606625", $mode);
&test_importer ("$samples/sc/demo_math", "fc220c4c3305228ec31e63b3528b788915889d71", $mode);
