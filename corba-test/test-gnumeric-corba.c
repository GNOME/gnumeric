/*
 * Sample excerciser of Gnumeric CORBA interface
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 */
#include <gnumeric-config.h>
#include <Gnumeric.h>
#include <libgnorba/gnorba.h>

CORBA_Environment ev;
CORBA_ORB orb;

static void
do_test (GNOME_Gnumeric_Workbook workbook)
{
	GNOME_Gnumeric_Sheet sheets [4], sheet0;
	GNOME_Gnumeric_Value *value;
	int i;

	printf ("1. Creating 4 new sheets...\n");
	for (i = 0; i < 4; i++){
		char *name;

		name = g_strdup_printf ("My Sheet %d", i);
		sheets [i] = GNOME_Gnumeric_Workbook_sheet_new (workbook, name, &ev);
		if (ev._major != CORBA_NO_EXCEPTION){
			printf ("Failed\n");
			exit (1);
		}
		g_free (name);
	}
	printf ("ok\n");

#if 0
	printf ("2. Trying to create an existing sheet name: ");
	GNOME_Gnumeric_Workbook_sheet_new (workbook, "My Sheet0", &ev);
	if (ev._major != CORBA_USER_EXCEPTION){
		printf ("Failed\n");
		exit (1);
	}
	printf ("    Got exception: %s\n", CORBA_exception_id (&ev));
	printf ("ok\n");


	printf ("3. Looking up `Sheet 0':");
	sheet0 = GNOME_Gnumeric_Workbook_sheet_lookup (workbook, "Sheet0", &ev);
	if (sheet0){
		printf ("Failed\n");
		exit (1);
	}
	printf ("ok\n");
#endif
	
	printf ("4. Filling a table...");
	value = GNOME_Gnumeric_Value__alloc ();
	for (i = 0; i < 25; i++){
	
		value->_d = GNOME_Gnumeric_VALUE_INTEGER;
		value->_u.v_int = i;
		
		GNOME_Gnumeric_Sheet_cell_set_value (sheets [0], 0, i+3, value, &ev);
		if (ev._major != CORBA_NO_EXCEPTION){
			printf ("Failed\n");
			exit (1);
		}
	}
	value->_d = GNOME_Gnumeric_VALUE_STRING;
	value->_u.str = CORBA_string_dup ("Hello!");
	GNOME_Gnumeric_Sheet_cell_set_value (sheets [0], 0, 0, value, &ev);
	CORBA_free (value);
	printf ("ok\n");


	printf ("5. Setting texts...");
	for (i = 0; i < 25; i++){
		char buffer [30];

		sprintf (buffer, "I am row %d", i);
		GNOME_Gnumeric_Sheet_cell_set_text (sheets [0], 1, i+3, buffer, &ev);
		if (ev._major != CORBA_NO_EXCEPTION){
			printf ("Failed\n");
			exit (1);
		}
	}
	printf ("ok\n");
}

int
main (int argc, char *argv[])
{
	GNOME_Gnumeric_Workbook workbook;
	
	CORBA_exception_init (&ev);
	orb = gnome_CORBA_init ("Gnumeric client test", "1.0", &argc, argv, 0, &ev);

	workbook = goad_server_activate_with_id (
		NULL, "GOADID:GNOME:Gnumeric:Workbook:1.0", 0, NULL);

	if (workbook == CORBA_OBJECT_NIL){
		printf ("Cannot bind workbook");
		exit (1);
	}

	/*
	 * Show the workbook
	 */
	GNOME_Gnumeric_Workbook_show (workbook, 1, &ev);
	
	do_test (workbook);
	
	CORBA_exception_free (&ev);
}

