#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the mps importer.");
&test_importer ("$samples/solver/blend.mps", "76ac41bbbd5d61d6d1828da7a2d332194099a962", $mode);
&test_importer ("$samples/solver/afiro.mps", "87fc37c3432db49ad95876e2d0da2b2739e72833", $mode);
