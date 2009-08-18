#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check that glade files parse.");
my $checker = "tools/check-glade-parsing";
&GnumericTest::report_skip ("Missing tester") unless -r "$topsrc/$checker";
&test_command ("cd $topsrc && $PERL $checker", sub { /^$/ });
