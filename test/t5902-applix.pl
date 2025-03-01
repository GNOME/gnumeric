#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

my $args = { 'mode' => $mode, 'nofont' => 1 };

&message ("Check the applix importer.");
&test_importer ("$samples/applix/sample.as", "3169d790e17a77fb0b4eaf4d566626e724256661", $args);
