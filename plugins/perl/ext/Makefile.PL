use ExtUtils::MakeMaker;

WriteMakefile('NAME' => 'Gnumeric',
              'LIBS' => `gnome-config --libs gnomeui`,
              'INC' => `gnome-config --cflags gnomeui`,
	      'VERSION_FROM' => 'Gnumeric.pm');
