#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

my $args = { 'mode' => $mode, 'nofont' => 1 };

&message ("Check the applix importer.");
&test_importer ("$samples/applix/sample.as", "c94917df732dfd6d84a571e434dbeadcb7adc6f1", $args);
