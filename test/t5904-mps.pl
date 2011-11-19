#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the mps importer.");
&test_importer ("$samples/solver/blend.mps", "453b70d7cb662fb7ad646defb48b01f5123b7209", $mode);
&test_importer ("$samples/solver/afiro.mps", "ba07e9d4b7557f38dd418af27880ace2bbe8e104", $mode);
