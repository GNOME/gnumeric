#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sylk importer.");
&test_importer ("$samples/sylk/test.sylk", "0bd6d4dde7dafa4db2e778f812cda1224cbbef6e", $mode);
&test_importer ("$samples/sylk/encoding.sylk", "ce1409a2d2116a754b805e6fdb8ef41219a7a072", $mode);
&test_importer ("$samples/sylk/app_b2.sylk", "06c1ad3d133a0e0ad0d9a885572413605c8a00fc", $mode);
