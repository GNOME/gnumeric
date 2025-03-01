#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

my $args = { 'mode' => $mode };

&message ("Check the guppi graph importer.");
&test_importer ("$samples/b66666-guppi.gnumeric", "d0457bcfae1b295a97aaae46357ffe0829b6935f", $args);
