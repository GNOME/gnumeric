#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check sstest with valgrind.");

&test_valgrind ("$sstest all >/dev/null 2>&1", 1);
