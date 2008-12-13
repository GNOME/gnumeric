#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the applix importer.");
&test_importer ("$samples/applix/sample.as", "f3b0c75693b93933da82512e59b11d78a43972a7", $mode);
