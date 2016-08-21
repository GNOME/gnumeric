#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check sstest with valgrind.");

my $pango = `pkg-config --modversion pango 2>/dev/null`;
chomp $pango;
&GnumericTest::report_skip ("Pango version $pango is buggy")
    if $pango eq '1.24.2';

&test_valgrind ("$sstest --fast all >/dev/null 2>&1", 1);
