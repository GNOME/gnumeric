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
Using in-tree gi.overrides.Gnm

Testing GnmValue overrides:
{EMPTY,None}
{BOOLEAN,0}
{BOOLEAN,1}
{FLOAT,12}
{FLOAT,12.5}
{STRING,"howdy"}
{FLOAT,12.5,"0.00"}

Testing GnmRange overrides:
B3:D5
