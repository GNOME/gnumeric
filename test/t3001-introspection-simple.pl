#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&setup_python_environment ();

my $python_script = $0;
$python_script =~ s/\.pl$/.py/;
my $ref = join("",<DATA>);
&test_command ($PYTHON . ' ' . &GnumericTest::quotearg ($python_script),
	       sub { $_ eq $ref });

__DATA__
Number of columns: 256
Number of rows: 65536

As string:
10
101.25
111.25
01
zzz
abc
TRUE

As int:
10
101
111
1
0
0
1

List of cells in sheet:
A1
A2
A3
A4
A5
A6
A7
