#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sylk importer.");
&test_importer ("$samples/sylk/test.sylk", "d213d55662f13d743b3aeaa12943f97b9b75675a", $mode);
&test_importer ("$samples/sylk/encoding.sylk", "fb374a088a958e962119d65333f16744642223dd", $mode);
&test_importer ("$samples/sylk/app_b2.sylk", "b92205dce72d8b971e9f2213d9b91aa17516b42a", $mode);
