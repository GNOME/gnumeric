use strict;

my $state = 0;

while (<>) {
    s/\s+$//;
    if (/^\@FUNCTION=(.*)/) {
	if ($state) {
	    printf "\n";
	    print "      </refsect1>\n";
	    print "    </refentry>\n\n";
	}
	my $func = $1;
	my $mod_func = &fixup_function_name ($1);
	$state = 0;
	print "\n\n";
	print "  <refentry id=\"gnumeric-$mod_func\">\n";
	print "    <refmeta>\n";
	print "      <refentrytitle>$func</refentrytitle>\n";
	print "    </refmeta>\n";
	print "    <refnamediv>\n";
	print "      <refname>$func</refname>\n";
	print "      <refpurpose></refpurpose>\n";
	print "    </refnamediv>\n";
	next;
    }

    if (/^\@SYNTAX=(.*)/) {
	print "    <refsynopsisdiv>\n";
	print "      <synopsis>", &quote_stuff ($1), "</synopsis>\n";
	print "    </refsynopsisdiv>\n";
	next;
    }

    if (/^\@DESCRIPTION=(.*)/) {
	print "      <refsect1>\n";
	print "        <title>Description</title>\n";
	print "        <para>", &quote_stuff ($1), "</para>\n";
	$state = 1;
	next;
    }

    if (/^\@EXAMPLES=(.*)/) {
	if ($state) {
	    print "\n    </refsect1>";
	}
	print "      <refsect1>\n";
	print "        <title>Examples</title>\n";
	print "        <para>", &quote_stuff ($1), "</para>\n";
	$state = 2;
	next;
    }

    if (/^\@SEEALSO=(.*)/) {
	my $linktxt = $1;
	$linktxt =~ s/\s//g;
	$linktxt =~ s/\.$//;
	my @links = split (/,/, $linktxt);

	if ($state) {
	    print "\n    </refsect1>";
	}
	print "\n    <refsect1><title>See also</title>\n";
	my @a = ();
	print   "      <para>";
	foreach my $link (@links) {
	    my $fixed_name = &fixup_function_name ($link);
	    push @a, "        <link linkend=\"gnumeric-$fixed_name\">$link</link>";
	}
	if (@a > 0) {
	    print join (",\n", @a), ".\n";
	}
	print "      </para>\n";
	print "    </refsect1>\n";
	print "  </refentry>\n\n";
	$state = 0;
	next;
    }

    if ($state) {
	print "        <para>", &quote_stuff ($_), "</para>";
    } else {
    }
}


sub quote_stuff {
    my ($str) = @_;

    # Let's do this one first...
    $str =~ s/\&/\&amp;/g;

    $str =~ s/</\&lt;/g;
    $str =~ s/>/\&gt;/g;
    return $str;
}

#Subroutine MUST agree with the subroutine in make-index.pl
sub fixup_function_name {
    my ($name) = @_;
    $name =~ s/_/x/;
    return $name;
}
