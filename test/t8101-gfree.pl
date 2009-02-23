#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check that code uses g_free and g_strdup right.");
my $checker = "tools/check-gfrees";
&GnumericTest::report_skip ("Missing tester") unless -r "$topsrc/$checker";
&test_command ("cd $topsrc && $PERL tools/check-gfrees", sub { /^$/ });
