#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check that code does not mix NULL and FALSE.");
my $checker = "tools/check-null-false-returns";
&GnumericTest::report_skip ("Missing tester") unless -r "$topsrc/$checker";
&test_command ("cd $topsrc && $PERL $checker", sub { /^$/ });
