$state = 0;

while (<>){
  if (/^\@FUNCTION=(.*)/){
    if ($state){
	printf "\n";
        print "      </refsect1>\n";
        print "    </refentry>\n\n";
    }
    $func=$1;
    $state = 0;
    print "\n\n";
    print "  <refentry>\n";
    print "    <refmeta>\n";
    print "      <refentrytitle><anchor id=\"gnumeric-$1\">$1</refentrytitle><refmiscinfo></refmiscinfo>\n";
    print "    </refmeta>\n";
    print "    <refnamediv>\n";
    print "      <refname>$1</refname>\n";
    print "      <refpurpose></refpurpose>\n";
    print "    </refnamediv>\n";
    next;
  }	
  
  if (/^\@SYNTAX=(.*)/){
    print "    <refsynopsisdiv>\n";
    print "      <synopsis>$1</synopsis>\n";
    print "    </refsynopsisdiv>\n";
    next;
  }
  
  if (/^\@DESCRIPTION=(.*)/){
    print "      <refsect1>\n";
    print "        <title>Description</title>\n";
    print "        <para>$1</para>\n";
    $state = 1;
    next;
  } 

  if (/^\@SEEALSO=(.*)/){
    @links = split (/,/, $1);

    print "\n    </refsect1>";
    print "\n    <refsect1><title>See also</title>\n";
    @a = ();
    print   "      <para>";
    foreach $link  (@links){
      $link =~ s/ //g;

       push @a, "        <link linkend=\"gnumeric-$link\">$link</link>\n";

    }
    print join (", ", @a);
    print "      </para>\n";
    print "    </refsect1>\n";
    print "  </refentry>\n\n";
    $state = 0;
    next;
  }

  if ($state){
    chop;
    print "        <para>$_</para>";
  } else {
  }
}
