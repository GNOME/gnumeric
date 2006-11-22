#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

print STDERR "Warning: tests are run using installed plugins.  (\"make install\".)\n";
print STDERR "Warning: you have a ~/.valgrindrc file that might affect tests.\n"
    if exists $ENV{'HOME'} && -r ($ENV{'HOME'} . "/.valgrindrc");

print STDERR "Pass\n";
