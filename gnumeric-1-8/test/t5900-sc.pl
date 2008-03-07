#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sc importer.");
&test_importer ("$samples/sc/demo_func", "4c087eee7a0ee8814e11338b2a6b1f365021c23c", $mode);
&test_importer ("$samples/sc/demo_math", "0996bb36fd49bb0d94bf60d97aaea25abd18d6ca", $mode);
