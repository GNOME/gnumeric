#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "$samples/style-tests.gnumeric";

&message ("Check style gnumeric roundtrip.");
&test_roundtrip ($file,
		 'format' => 'Gnumeric_XmlIO:sax',
		 'ext' => "gnm");

&message ("Check style ods roundtrip.");
&test_roundtrip ($file,
		 'format' => 'Gnumeric_OpenCalc:odf',
		 'ext' => "ods",
		 'filter2' => "$PERL -p -e '\$_ = \"\" if m{<meta:generator>}'");

my $xls_codepage_filter = "$PERL -p -e '\$_ = \"\" if m{<meta:user-defined meta:name=.msole:codepage.}'";
my $xls_rotation_filter = "$PERL -p -e 's{\\b(Rotation)=\"315\"}{\$1=\"270\"}; s{\\b(Rotation)=\"45\"}{\$1=\"0\"};'";

&message ("Check style xls/BIFF7 roundtrip.");
&test_roundtrip ($file,
		 'format' => 'Gnumeric_Excel:excel_biff7',
		 'ext' => "xls",
		 'resize' => '16384x256',
		 'filter1' => $xls_rotation_filter,
		 'filter2' => $xls_codepage_filter ,
		 'ignore_failure' => 1);

&message ("Check style xls/BIFF8 roundtrip.");
&test_roundtrip ($file,
		 'format' => 'Gnumeric_Excel:excel_biff8',
		 'ext' => "xls",
		 'filter2' => $xls_codepage_filter,
		 'ignore_failure' => 1);

&message ("Check style xlsx roundtrip.");
&test_roundtrip ($file,
		 'format' => 'Gnumeric_Excel:xlsx',
		 'ext' => "xlsx",
		 'resize' => '1048576x16384',
		 'ignore_failure' => 1);
