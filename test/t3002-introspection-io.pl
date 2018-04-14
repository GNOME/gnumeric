#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&setup_python_environment ();

my $python_script = $0;
$python_script =~ s/\.pl$/.py/;

for my $file ('format-tests.gnumeric') {
    my $src = "$samples/$file";
    my $dst = $file;
    $dst =~ s{\.([^./]+)$}{-copy.$1};

    unlink $dst;
    &GnumericTest::junkfile ($dst);

    &test_command ($PYTHON . ' ' .
		   &GnumericTest::quotearg ($python_script, $src, $dst),
		   sub { 1 });

    &test_command (&GnumericTest::quotearg ($ssdiff, '--xml', $src, $dst),
		   sub { 1 });

    &GnumericTest::removejunk ($dst);
}
