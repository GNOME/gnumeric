/*
 * corba-workbook.c: CORBA Workbook exporting.
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 */
#include <config.h>
#include <libgnorba/gnome-factory.h>
#include <gnome.h>
#include <bonobo/gnome-object.h>
#include "sheet.h"
#include "gnumeric.h"
#include "Gnumeric.h"
#include "xml-io.h"
#include "corba.h"

typedef struct {
	POA_GNOME_Gnumeric_Workbook servant;	
	Workbook *workbook;
} WorkbookServant;

static POA_GNOME_Gnumeric_Workbook__vepv gnome_gnumeric_workbook_vepv;
static POA_GNOME_Gnumeric_Workbook__epv gnome_gnumeric_workbook_epv;

static Workbook *
workbook_from_servant (PortableServer_Servant servant)
{
	WorkbookServant *ws = (WorkbookServant *) servant;

	return ws->workbook;
}

static inline GNOME_Gnumeric_Sheet
corba_sheet (Sheet *sheet, CORBA_Environment *ev)
{
	return CORBA_Object_duplicate (sheet->corba_server, ev);
}

static GNOME_Gnumeric_Sheet
Workbook_sheet_new (PortableServer_Servant servant, const CORBA_char * name, CORBA_Environment * ev)
{
	GNOME_Gnumeric_Sheet ggs;
        Workbook *workbook = workbook_from_servant (servant);
	Sheet *sheet;

	if (workbook_sheet_lookup (workbook, name)){
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION, ex_GNOME_Gnumeric_Workbook_NameExists, NULL);
		return CORBA_OBJECT_NIL;
	}
	
	sheet = sheet_new (workbook, name);

	workbook_attach_sheet (workbook, sheet);
	
	return corba_sheet (sheet, ev);
}

static GNOME_Gnumeric_Sheet
Workbook_sheet_lookup (PortableServer_Servant servant, const CORBA_char * name, CORBA_Environment * ev)
{
        Workbook *workbook = workbook_from_servant (servant);
	Sheet *sheet;

	sheet = workbook_sheet_lookup (workbook, name);
	if (sheet == NULL)
		return CORBA_OBJECT_NIL;

	return corba_sheet (sheet, ev);
}

static void
Workbook_set_filename (PortableServer_Servant servant, const CORBA_char * name, CORBA_Environment * ev)
{
        Workbook *workbook = workbook_from_servant (servant);

	workbook_set_filename (workbook, name);
}

static void
Workbook_save_to (PortableServer_Servant servant, const CORBA_char * filename, CORBA_Environment * ev)
{
        Workbook *workbook = workbook_from_servant (servant);

	gnumericWriteXmlWorkbook (workbook, filename);
}

static GNOME_Gnumeric_Sheet
Workbook_sheet_current (PortableServer_Servant servant, CORBA_Environment * ev)
{
        Workbook *workbook = workbook_from_servant (servant);
	Sheet *sheet = workbook_get_current_sheet (workbook);

	return corba_sheet (sheet, ev);
}

static GNOME_Gnumeric_Sheet
Workbook_sheet_nth (PortableServer_Servant servant, const CORBA_long n, CORBA_Environment * ev)
{
	g_error ("Same stuff!");

	return CORBA_OBJECT_NIL;
}

static CORBA_long
Workbook_sheet_count (PortableServer_Servant servant, CORBA_Environment * ev)
{
        Workbook *workbook = workbook_from_servant (servant);

	return workbook_sheet_count (workbook);
}

static void
Workbook_set_dirty (PortableServer_Servant servant, const CORBA_boolean is_dirty, CORBA_Environment * ev)
{
        Workbook *workbook = workbook_from_servant (servant);

	workbook_set_dirty (workbook, is_dirty);
}

static CORBA_boolean
Workbook_sheet_rename (PortableServer_Servant servant,
		       const CORBA_char * old_name,
		       const CORBA_char * new_name, CORBA_Environment * ev)
{
	Workbook *workbook = workbook_from_servant (servant);
	
	return workbook_rename_sheet (workbook, old_name, new_name);
}

static void
Workbook_recalc (PortableServer_Servant servant, CORBA_Environment * ev)
{
	Workbook *workbook = workbook_from_servant (servant);

	workbook_recalc (workbook);
}

static void
Workbook_recalc_all (PortableServer_Servant servant, CORBA_Environment * ev)
{
	Workbook *workbook = workbook_from_servant (servant);

	workbook_recalc_all (workbook);
}

static void
Workbook_parse (PortableServer_Servant servant,
		const CORBA_char * cellref,
		GNOME_Gnumeric_Sheet * sheet,
		CORBA_long * col,
		CORBA_long * row, CORBA_Environment * ev)
{
}

static void
Workbook_corba_class_init ()
{
	static int inited;

	if (inited)
		return;
	inited = TRUE;

	gnome_gnumeric_workbook_vepv.GNOME_Gnumeric_Workbook_epv =
		&gnome_gnumeric_workbook_epv;
	gnome_gnumeric_workbook_vepv.GNOME_object_epv =
		&gnome_object_epv;

	gnome_gnumeric_workbook_epv.sheet_new = Workbook_sheet_new;
	gnome_gnumeric_workbook_epv.sheet_lookup = Workbook_sheet_lookup;
	gnome_gnumeric_workbook_epv.set_filename = Workbook_set_filename;
	gnome_gnumeric_workbook_epv.save_to = Workbook_save_to;
	gnome_gnumeric_workbook_epv.sheet_current = Workbook_sheet_current;
	gnome_gnumeric_workbook_epv.sheet_nth = Workbook_sheet_nth;
	gnome_gnumeric_workbook_epv.sheet_count = Workbook_sheet_count;
	gnome_gnumeric_workbook_epv.set_dirty = Workbook_set_dirty;
	gnome_gnumeric_workbook_epv.sheet_rename = Workbook_sheet_rename;
	gnome_gnumeric_workbook_epv.recalc = Workbook_recalc;
	gnome_gnumeric_workbook_epv.recalc_all = Workbook_recalc_all;
	gnome_gnumeric_workbook_epv.parse = Workbook_parse;
}

void
workbook_corba_setup (Workbook *workbook)
{
	WorkbookServant *ws;
	CORBA_Environment ev;
        PortableServer_ObjectId *objid;
	
	Workbook_corba_class_init ();

	ws = g_new0 (WorkbookServant, 1);
	ws->servant.vepv = &gnome_gnumeric_workbook_vepv;
	ws->workbook = workbook;

	CORBA_exception_init (&ev);
	POA_GNOME_Gnumeric_Workbook__init ((PortableServer_Servant) ws, &ev);
	objid = PortableServer_POA_activate_object (gnumeric_poa, ws, &ev);
	CORBA_free (objid);
	workbook->corba_server = PortableServer_POA_servant_to_reference (gnumeric_poa, ws, &ev);
	
	CORBA_exception_free (&ev);
}

void
workbook_corba_shutdown (Workbook *wb)
{
	CORBA_Environment ev;
	
	g_return_if_fail (wb != NULL);
	g_return_if_fail (wb->corba_server != NULL);

	g_warning ("Should release all the corba resources here");

	CORBA_exception_init (&ev);
	PortableServer_POA_deactivate_object (gnumeric_poa, wb->corba_server, &ev);
	CORBA_exception_free (&ev);
}
       

