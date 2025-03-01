#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

my $args = { 'mode' => $mode, 'nofont' => 1 };

&message ("Check the mps importer.");
&test_importer ("$samples/solver/blend.mps", "b11ee3fc38845d13eb8b57c8e17ae764d56386f2", $args);
&test_importer ("$samples/solver/afiro.mps", "467180786aac14a58955335fb6e00d4652cedfbf", $args);
