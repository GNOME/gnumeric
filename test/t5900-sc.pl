#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sc importer.");
&test_importer ("$samples/sc/demo_func", "a08063fecf9d6c3cd0bb31dbf3056631f4deb295", $mode);
&test_importer ("$samples/sc/demo_math", "fd05f09413af4be8a1bc01462bd0ad9f963abef8", $mode);
