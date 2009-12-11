#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sylk importer.");
&test_importer ("$samples/sylk/test.sylk", "b71727195e4c854f139eb3512d025ff152593b7a", $mode);
&test_importer ("$samples/sylk/encoding.sylk", "084ef2fe0a43fac23b1d8f3d83be02c8396e9104", $mode);
&test_importer ("$samples/sylk/app_b2.sylk", "9158ff7b315a63657b24f746f80cc5cef971b15e", $mode);
