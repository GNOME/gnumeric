#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the guppi graph importer.");
&test_importer ("$samples/b66666-guppi.gnumeric", "e7d2f225b9fc36dae36291d4d2fa848d39e7b006", $mode);
