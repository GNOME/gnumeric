#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sc importer.");
&test_importer ("$samples/sc/demo_func", "4fb115f8b93d5f4ff826a38dc67ca4e74671de6a", $mode);
&test_importer ("$samples/sc/demo_math", "f63b59828741248ce314b4c4eeba93b0dad1e74d", $mode);
