#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sylk importer.");
&test_importer ("$samples/sylk/test.sylk", "369c05197d7e37a32fb74f5aee7e2138eedd93dc", $mode);
&test_importer ("$samples/sylk/encoding.sylk", "c3ef8e44a76a601bd5951d56046b1ad8c4dab8db", $mode);
