CFLAGS  = `gnome-config --cflags bonobo` -I../idl
LDFLAGS = `gnome-config --libs bonobo`
LDADD   = -L../idl -lGnumeric
noinst_PROGRAMS = test-gnumeric-corba

test_gnumeric_corba_SOURCES =	\
	test-gnumeric-corba.c 

