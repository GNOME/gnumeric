use strict;

my $state = 0;
my $cat = "";
my $func = "";

while (<>) {
    s/\s+$//;
    if (/^\@CATEGORY=(.*)/) {
	if ($state) {
	    print "    </refsect1>\n";
	    print "  </refentry>\n\n";
	}
	if ($cat ne $1) {
	    if ($cat ne "") {
		print "</sect1>\n";
	    }
	    $cat = $1;
	    my $cat_id = "CATEGORY_" . $cat;
	    $cat_id =~ s/\s+/_/g;
	    $cat_id =~ s/[^A-Za-z_]//g;
	    print "<sect1 id=\"$cat_id\">\n";
	    print "  <title>", &quote_stuff ($cat), "</title>\n";
	}
	$state = 0;
    }
    if (/^\@FUNCTION=(.*)/) {
	if ($state) {
	    if ($state == 3) {
		print "        </itemizedlist>\n";
	    }
	    print "      </refsect1>\n";
	    print "    </refentry>\n\n";
	}
	$func = $1;
	my $mod_func = &fixup_function_name ($1);
	$state = 0;
	print "\n\n";
	print "  <refentry id=\"gnumeric-$mod_func\">\n";
	print "    <refmeta>\n";
	print "      <refentrytitle><function>$func</function></refentrytitle>\n";
	print "    </refmeta>\n";
	print "    <refnamediv>\n";
	print "      <refname><function>$func</function></refname>\n";
	print "    </refnamediv>\n";
	next;
    }

    if (/^\@SYNTAX=(.*)/) {
	my $str = &markup_stuff ($1);
	$str =~ s/([\(\,])(\w*)/\1<parameter>\2<\/parameter>/g;
	print "    <refsynopsisdiv>\n";
	print "      <synopsis>$str</synopsis>\n";
	print "    </refsynopsisdiv>\n";
	next;
    }

    if (/^\@DESCRIPTION=(.*)/) {
	print "    <refsect1>\n";
	print "      <title>Description</title>\n";
	print "      <para>", &markup_stuff ($1), "</para>\n";
	$state = 1;
	next;
    }

    if (/^\@EXAMPLES=(.*)/) {
	if ($state) {
	    if ($state == 3) {
		print "        </itemizedlist>\n";
	    }
	    print "    </refsect1>\n";
	}
	print "    <refsect1>\n";
	print "      <title>Examples</title>\n";
	print "      <para>", &markup_stuff ($1), "</para>\n";
	$state = 2;
	next;
    }

    if (/^\@SEEALSO=(.*)/) {
	my $linktxt = $1;
	$linktxt =~ s/\s//g;
	$linktxt =~ s/\.$//;
	my @links = split (/,/, $linktxt);

	if ($state) {
	    if ($state == 3) {
		print "        </itemizedlist>\n";
	    }
	    print "    </refsect1>\n";
	}
	print "    <refsect1>\n";
	print "      <title>See also</title>\n";
	my @a = ();
	print   "      <para>\n";
	foreach my $link (@links) {
	    my $fixed_name = &fixup_function_name ($link);
	    push @a, "        <link linkend=\"gnumeric-$fixed_name\"><function>$link</function></link>";
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
	if (/^\*\s/) {
	    my $str = &markup_stuff ($_);
	    $str =~ s/^\*\s+//;
	    if ($state ne 3) {
		print "        <itemizedlist>\n";
		$state = 3;
	    }
	    print "          <listitem><para>$str</para></listitem>\n";
	}
	elsif ($_ ne "") {
	    if ($state == 3) {
		print "        </itemizedlist>\n";
		$state = 1;
	    }
	    print "        <para>", &markup_stuff ($_), "</para>\n";
	}
    }
}

print "</sect1>\n";

sub markup_stuff {
    my ($str) = @_;

    $str = &quote_stuff ($str);

    $str =~ s/\b$func\b/<function>$func<\/function>/g;
    $str =~ s/\@\{(\w*)\}/<parameter>\1<\/parameter>/g;
    $str =~ s/\@(\w*)\b/<parameter>\1<\/parameter>/g;

    return $str;
}

sub quote_stuff {
    my ($str) = @_;

    # Let's do this one first...
    $str =~ s/\&/\&amp;/g;

    $str =~ s/</\&lt;/g;
    $str =~ s/>/\&gt;/g;
    return $str;
}

sub fixup_function_name {
    my ($name) = @_;
#    why did we need this ?  leave the routine here just in case
#    $name =~ s/_/x/g;
    return $name;
}

