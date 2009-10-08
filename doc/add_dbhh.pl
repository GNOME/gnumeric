#!/usr/bin/perl
use strict;
use warnings;

use Data::Dumper;
use HTML::Parser ();
use HTML::Entities ();

use Getopt::Long;

my $indent = 2;
my $out_dir = 'output';
my $map_file = 'hhmap';

binmode(STDOUT, ":utf8");

GetOptions(
    'indent=s'  => \$indent,
    'out-dir=s' => \$out_dir,
    'map-file=s' => \$map_file,
);

my @list;

sub start {
    my ($tagname, $attr, $offset_end, $column) = @_;

    if ($tagname =~ /\Asect[0-9]\Z/i) {
        my %attr;

        while (my ($k, $v) = each %$attr) {
            $attr{lc($k)} = $v;
        }

        push @list, [$offset_end, $column, $attr{id}];
    }
}

my $id = 10;

mkdir $out_dir;
open MAP, '>:utf8', "$map_file";

foreach my $file (<*.xml>) {
    open FH, "<:utf8", $file;
    my $xml = do { local $/; <FH>; };
    close FH;

    @list = ();

    my $p = HTML::Parser->new(
        api_version => 3,
        start_h => [\&start, "tagname, attr, offset_end, column"],
        marked_sections => 1,
    );

    $p->parse($xml);

    my $last = 0;

    open FH, '>:utf8', "$out_dir/$file";

    foreach my $sect (@list) {
        my ($offset, $column, $name) = @$sect;
        print FH substr($xml, $last, ($offset - $last));
        print FH "\n" . (' ' x ($column + $indent)) . "<?dbhh topicname=\"$name\" topicid=\"$id\"?>\n";
        print MAP "$id\t$name\n";
        $last = $offset;
        ++$id;
    }

    print FH substr($xml, $last);

    close FH;
}
close MAP;

# vi: set autoindent shiftwidth=4 tabstop=8 softtabstop=4 expandtab:
