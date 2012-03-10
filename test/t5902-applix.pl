#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the applix importer.");
&test_importer ("$samples/applix/sample.as", "43497bc5daaf91ad71a609e638a78eb414375307", $mode);
