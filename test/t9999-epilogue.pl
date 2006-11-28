#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Warnings about things that might affect tests.");

my $HOME = exists $ENV{'HOME'};

# ----------------------------------------

print STDERR "Warning: tests are run using installed plugins.  (\"make install\".)\n";

# ----------------------------------------

print STDERR "Warning: you have a ~/.valgrindrc file that might affect tests.\n"
    if defined ($HOME) && -r "$HOME/.valgrindrc";

# ----------------------------------------

my $deffont = `gconftool-2 -g /apps/gnumeric/core/defaultfont/name`;
chomp $deffont;
if ($deffont ne 'Sans') {
    print STDERR "Warning: the default font is \"$deffont\", not \"Sans\".  This may affect tests.\n";
}

# ----------------------------------------

print STDERR "Pass\n";
