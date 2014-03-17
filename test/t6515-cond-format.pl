#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "$samples/cond-format-tests.gnumeric";

&message ("Check conditional format gnumeric roundtrip.");
&test_roundtrip ($file,
		 'format' => 'Gnumeric_XmlIO:sax',
		 'ext' => "gnm");

&message ("Check conditional format ods roundtrip.");
&test_roundtrip ($file,
		 'format' => 'Gnumeric_OpenCalc:odf',
		 'ext' => "ods",
		 'filter2' => "$PERL -p -e '\$_ = \"\" if m{<meta:generator>}'",
		 'ignore_failure' => 1);

my $xls_codepage_filter = "$PERL -p -e '\$_ = \"\" if m{<meta:user-defined meta:name=.msole:codepage.}'";

# xls/biff cannot handle format in conditional formats.
my $xls_cond_format_filter = "$PERL -p -e 'if (m{<gnm:Condition\\b} ... m{</gnm:Condition\\b}) { s{\\s+Format=\"[^\"\"]*\"}{}; }'";
print STDERR "$xls_cond_format_filter\n";

&message ("Check conditional format xls/BIFF7 roundtrip.");
&test_roundtrip ($file,
		 'format' => 'Gnumeric_Excel:excel_biff7',
		 'ext' => "xls",
		 'resize' => '16384x256',
		 'filter1' => $xls_cond_format_filter,
		 'filter2' => $xls_codepage_filter,
		 'ignore_failure' => 1);

&message ("Check conditional format xls/BIFF8 roundtrip.");
&test_roundtrip ($file,
		 'format' => 'Gnumeric_Excel:excel_biff8',
		 'ext' => "xls",
		 'filter1' => $xls_cond_format_filter,
		 'filter2' => $xls_codepage_filter,
		 'ignore_failure' => 1);

if (0) {
&message ("Check conditional format xlsx roundtrip.");
&test_roundtrip ($file,
		 'format' => 'Gnumeric_Excel:xlsx',
		 'ext' => "xlsx",
		 'resize' => '1048576x16384',
		 'ignore_failure' => 1);
}
