#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

# See test_importer comments for mode definitions.
my $mode = ((shift @ARGV) || "check");

&message ("Check the sc importer.");
&test_importer ("$samples/sc/demo_func", "d478d1673623e293440ef101a99808691c0aeee4", $mode);
&test_importer ("$samples/sc/demo_math", "b181d4d5deb58d9905dfd479ba8ca56fb09da128", $mode);
