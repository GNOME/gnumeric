#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sylk importer.");
&test_importer ("$samples/sylk/test.sylk", "9ab30dce90e2df3ba7a325b0f971c3341da7b258", $mode);
&test_importer ("$samples/sylk/encoding.sylk", "96752054824a64eb413c40c32e8874be0778114f", $mode);
&test_importer ("$samples/sylk/app_b2.sylk", "fa9f341a1dfbb5def1181c53a3d74f2c4dd9facc", $mode);
