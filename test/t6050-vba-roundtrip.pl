#!/usr/bin/perl -w
# -----------------------------------------------------------------------------

use strict;
use lib ($0 =~ m|^(.*/)| ? $1 : ".");
use GnumericTest;

&message ("Check that vba roundtrips through xls");

my $src = "$samples/vba-725220.xls";
&GnumericTest::report_skip ("file $src does not exist") unless -r $src;

my $gsf = &GnumericTest::find_program ("gsf");

my $dir1 = &gsf_list ($src);

my $tmp = $src;
$tmp =~ s|^.*/||;
$tmp =~ s|\..*|-tmp.xls|;
&GnumericTest::junkfile ($tmp);
system ("$ssconvert $src $tmp");
my $dir2 = &gsf_list ($tmp);

foreach my $f (sort keys %$dir1) {
    next unless ($f eq "\001Ole" ||
		 $f eq "\001CompObj" ||
		 $f =~ m{^_VBA_PROJECT_CUR/});
    my $fprint = $f;
    $fprint =~ s{\001}{\\001};
    if (!exists $dir2->{$f}) {
	die "$0: member $fprint is missing after conversion.\n";
    } elsif ($dir1->{$f} ne $dir2->{$f}) {
	die "$0: member $fprint changed length during conversion.\n";
    } else {
	my $d1 = `$gsf cat '$src' '$f'`;
	my $d2 = `$gsf cat '$tmp' '$f'`;
	if (length ($d1) ne $dir1->{$f}) {
	    print "Member $fprint is strange\n";
	} elsif ($d1 eq $d2) {
	    print "Member $fprint is ok\n";
	} else {
	    die "$0: member $fprint changed contents during conversion.\n";
	}
    }
}

sub gsf_list {
    my ($fn) = @_;

    my $dir = {};
    local (*FIL);
    open (FIL, "$gsf list '$fn' | ") or die "Cannot parse $fn: $!\n";
    while (<FIL>) {
	next unless /^f\s.*\s(\d+)\s+(.*)$/;
	$dir->{$2} = $1;
    }
    close FIL;
    return $dir;
}
