$state = 0;

while (<>) {
    s/\s+$//;
    if (/^\@FUNCTION=(.*)/) {
	if ($state) {
	    printf "\n";
	    print "      </refsect1>\n";
	    print "    </refentry>\n\n";
	}
	my $func = $1;
	$state = 0;
	print "\n\n";
	print "  <refentry>\n";
	print "    <refmeta>\n";
	print "      <refentrytitle><anchor id=\"gnumeric-$func\">$func</refentrytitle><refmiscinfo></refmiscinfo>\n";
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
	print "        <title>Описание</title>\n";
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
	print "\n    <refsect1><title>См. также</title>\n";
	my @a = ();
	print   "      <para>";
	foreach my $link (@links) {
	    push @a, "        <link linkend=\"gnumeric-$link\">$link</link>";
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

    $str =~ s/</\&lt;/g;
    $str =~ s/>/\&gt;/g;
    return $str;
}
