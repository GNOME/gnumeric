#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

# NOTE: this is an import/export tests.  We do not look at what is done
# with the formats in the test files.

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "$samples/rich-text-tests.gnumeric";

&message ("Check rich text gnumeric roundtrip.");
&test_roundtrip ($file,
		 'format' => 'Gnumeric_XmlIO:sax',
		 'ext' => "gnm");

&message ("Check rich text ods roundtrip.");
&test_roundtrip ($file,
		 'format' => 'Gnumeric_OpenCalc:odf',
		 'ext' => "ods",
		 'filter2' => "$PERL -p -e '\$_ = \"\" if m{<meta:generator>}'");

my $xls_codepage_filter = "$PERL -p -e '\$_ = \"\" if m{<meta:user-defined meta:name=.msole:codepage.}'";

# xls cannot have superscript and subscript at the same time
my $xls_supersub_filter = "$PERL -p -e 's{\\[superscript=1:3:5\\]\\[subscript=1:4:5\\]}{[superscript=1:3:4][subscript=1:4:5]};'";

&message ("Check rich text xls/BIFF7 roundtrip.");
&test_roundtrip ($file,
		 'format' => 'Gnumeric_Excel:excel_biff7',
		 'ext' => "xls",
		 'resize' => '16384x256',
		 'filter1' => $xls_supersub_filter,
		 'filter2' => $xls_codepage_filter,
		 'ignore_failure' => 1);

&message ("Check rich text xls/BIFF8 roundtrip.");
&test_roundtrip ($file,
		 'format' => 'Gnumeric_Excel:excel_biff8',
		 'ext' => "xls",
		 'filter1' => $xls_supersub_filter,
		 'filter2' => $xls_codepage_filter,
		 'ignore_failure' => 1);

&message ("Check rich text xlsx roundtrip.");
&test_roundtrip ($file,
		 'format' => 'Gnumeric_Excel:xlsx',
		 'ext' => "xlsx",
		 'resize' => '1048576x16384',
		 'ignore_failure' => 1);
