#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sc importer.");
&test_importer ("$samples/sc/demo_func", "53d6a70a2ee23b41c6756facc399b39a68076980", $mode);
&test_importer ("$samples/sc/demo_math", "af1ae27b470296a27f6525f3231729e02961b0b2", $mode);
