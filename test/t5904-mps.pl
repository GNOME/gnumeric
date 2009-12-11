#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the mps importer.");
&test_importer ("$samples/solver/blend.mps", "7f234ba9e698cbd702f68c6de979496b9c86d389", $mode);
&test_importer ("$samples/solver/afiro.mps", "e330c9a07934723184e69bcc467d19e1212c9245", $mode);
