
use strict;

print "<itemizedlist mark=\"bullet\">\n";

my @f = ();
while (<>){
  if (/^\@FUNCTION=(.*)/){
    push @f, $1;
  }
}

foreach my $func (sort @f){
  my $fixed_name = &fixup_function_name ($func);
  print "<listitem><para><link linkend=\"gnumeric-$fixed_name\">$func</link></para></listitem>\n";
}

print "</itemizedlist>\n";

#Subroutine MUST agree with the subroutine in make-docbook.pl
sub fixup_function_name {
    my ($name) = @_;
    $name =~ s/_/x/;
    return $name;
}
