#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the dif importer.");
&test_importer ("$samples/dif/sample.dif", "801fec04d73dd68da006184a2955e09ffe485bb4", $mode);
