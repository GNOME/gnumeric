#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sc importer.");
&test_importer ("$samples/sc/demo_func", "88703179026fc2b04d578065cbac437a2e317527", $mode);
&test_importer ("$samples/sc/demo_math", "bd756be543490d633349bc9c53217c987050f47c", $mode);
