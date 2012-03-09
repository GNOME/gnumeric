#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the mps importer.");
&test_importer ("$samples/solver/blend.mps", "b53a0f51a6c8f5cfd5b32f01655a200f231676af", $mode);
&test_importer ("$samples/solver/afiro.mps", "185dcde9d49c11506ba63756d69f835eb2ad23a9", $mode);
