#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

my $file = "$samples/style-tests.gnumeric";

if (&subtest ("gnumeric")) {
    &message ("Check style gnumeric roundtrip.");
    &test_roundtrip ($file,
		     'format' => 'Gnumeric_XmlIO:sax',
		     'ext' => "gnm");
}

if (&subtest ("ods")) {
    &message ("Check style ods roundtrip.");
    &test_roundtrip ($file,
		     'format' => 'Gnumeric_OpenCalc:odf',
		     'ext' => "ods",
		     'filter2' => 'std:drop_generator');
}

if (&subtest ("ods-strict")) {
    &message ("Check string ods strict-conformance roundtrip.");
    &test_roundtrip ($file,
		     'format' => 'Gnumeric_OpenCalc:openoffice',
		     'ext' => "ods",
		     'filter1' => 'std:ods_strict',
		     'filter2' => 'std:drop_generator | std:ods_strict',
		     'ignore_failure' => 1);
}

# Biff7 only handles a few fixed rotations.
my $xls_rotation_filter = "$PERL -p -e 's{\\b(Rotation)=\"315\"}{\$1=\"270\"}; s{\\b(Rotation)=\"45\"}{\$1=\"0\"};'";

# Biff7 has no diagonals patterns.
my $xls_diagonal_filter = "$PERL -p -e 'if (m{<gnm:StyleBorder>} .. m{</gnm:StyleBorder>}) { if (m{gnm:(Rev-)?Diagonal}) { \$_=\"\"; } else { \$any++; } \$save .= \$_; if (m{</gnm:StyleBorder>}) { print \$save if \$any>2; \$any = 0; \$save = \"\"; } \$_=\"\"; }'";

# Biff7 doesn't store indentation
my $xls_indent_filter = "$PERL -p -e 's{\\bIndent=\"[1-9]\\d*\"}{Indent=\"0\"};'";

# Our patterns 19-24 do not exist in xls
my $xls_pattern_filter = "$PERL -p -e 'use English; my \%m=(19,14,20,7,21,4,22,4,23,2,24,1); if (m{\\bShade=\"(\\d+)\"} && (\$n = \$m{\$1})) { \$_ = \"\${PREMATCH}Shade=\\\"\$n\\\"\${POSTMATCH}\"; }'";

if (&subtest ("biff7")) {
    &message ("Check style xls/BIFF7 roundtrip.");
    &test_roundtrip ($file,
		     'format' => 'Gnumeric_Excel:excel_biff7',
		     'ext' => "xls",
		     'resize' => '16384x256',
		     'filter1' => "$xls_rotation_filter | $xls_pattern_filter | $xls_diagonal_filter | $xls_indent_filter",
		     'filter2' => 'std:drop_codepage');
}

if (&subtest ("biff8")) {
    &message ("Check style xls/BIFF8 roundtrip.");
    &test_roundtrip ($file,
		     'format' => 'Gnumeric_Excel:excel_biff8',
		     'ext' => "xls",
		     'filter1' => $xls_pattern_filter,
		     'filter2' => 'std:drop_codepage');
}

if (&subtest ("xlsx")) {
    &message ("Check style xlsx roundtrip.");
    &test_roundtrip ($file,
		     'format' => 'Gnumeric_Excel:xlsx',
		     'ext' => "xlsx",
		     'resize' => '1048576x16384',
		     'filter1' => $xls_pattern_filter);
}
