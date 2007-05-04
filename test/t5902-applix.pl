#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the applix importer.");
&test_importer ("$samples/applix/sample.as", "f2bd60f93fada8a988a0c7eab76e594ed0984901", $mode);
