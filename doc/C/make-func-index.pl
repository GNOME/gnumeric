
print "<itemizedlist mark=\"bullet\">\n";

while (<>){
  if (/^\@FUNCTION=(.*)/){
    push @f, $1;
  }
}

foreach $func (sort @f){
  print "<listitem><para><link linkend=\"gnumeric-$func\">$func</link></para></listitem>\n";
}

print "</itemizedlist>\n";
