#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sylk importer.");
&test_importer ("$samples/sylk/test.sylk", "3d73af489169d82aabe80c831a7d8a5ca2b06d27", $mode);
&test_importer ("$samples/sylk/encoding.sylk", "e14b2bc7021d12e3af95df40dec60358935ebc52", $mode);
&test_importer ("$samples/sylk/app_b2.sylk", "fa64ab80d054670f3ac8b2866d64a10dbb1bf960", $mode);
