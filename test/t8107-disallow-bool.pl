#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check that no-one uses allow-none");

my @hits = `find $topsrc -type f -name '*.[ch]' -print0 | xargs -0 -n100 grep -n -w bool`;
chomp @hits;
@hits = map { [$_, $_] } @hits;

@hits = grep {
    $_->[1] =~ s{/\*.*/}{};
    $_->[1] =~ s{//.*}{};
    $_->[1] =~ s{^([^""]*)"([^""]+)"}{$1"string"};
    

    $_->[1] =~ /\bbool\b/;
} @hits;



if (@hits) {
    for my $h (@hits) {
	print STDERR "| ", $h->[0], "\n";
    }
    die "Fail.\n\n";
} else {
    print STDERR "Pass\n";
}
