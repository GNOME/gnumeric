#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Warnings about things that might affect tests.");

my $HOME = $ENV{'HOME'};

# ----------------------------------------

# No longer true.  We use the tree's plugins, at least on Linux.
#print STDERR "Warning: tests are run using installed plugins.  (\"make install\".)\n";

# ----------------------------------------

print STDERR "Warning: goal seek tests use random numbers.  Report sporadic failures.\n";

# ----------------------------------------

print STDERR "Warning: you have a ~/.valgrindrc file that might affect tests.\n"
    if defined ($HOME) && -r "$HOME/.valgrindrc";

# ----------------------------------------

my $deffont = `gconftool-2 -g /apps/gnumeric/core/defaultfont/name 2>/dev/null`;
chomp $deffont;
if ($deffont eq '') {
    print STDERR "Warning: the default font is not set.\n";
} elsif ($deffont ne 'Sans') {
    print STDERR "Warning: the default font is \"$deffont\", not \"Sans\".  This may affect tests.\n";
}

# ----------------------------------------

print STDERR "Pass\n";
