/*
 * corba-workbook-factory.c: CORBA Workbook factory.
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 */

#include <config.h>
#include <libgnorba/gnome-factory.h>
#include <gnome.h>
#include "sheet.h"
#include "gnumeric.h"
#include "Gnumeric.h"
#include "xml-io.h"
#include "corba.h"

PortableServer_POA gnumeric_poa;

typedef struct {
	POA_GNOME_Gnumeric_WorkbookFactory servant;
	PortableServer_POA poa;
} WorkbookFactoryServant;

/* The servant for the workbook factory */
static WorkbookFactoryServant workbook_factory_servant;

/* The workbook factory object */
static GNOME_Gnumeric_WorkbookFactory gnumeric_workbook_factory;

static PortableServer_ServantBase__epv gnumeric_workbook_factory_base_epv;
static POA_GNOME_GenericFactory__epv gnumeric_workbook_generic_factory_epv;
static POA_GNOME_Gnumeric_WorkbookFactory__epv gnumeric_workbook_factory_epv;
static POA_GNOME_Gnumeric_WorkbookFactory__vepv gnumeric_workbook_factory_vepv;

static GNOME_Gnumeric_Workbook
WorkbookFactory_read (PortableServer_Servant servant, const CORBA_char * filename, CORBA_Environment * ev)
{
	WorkbookFactoryServant *wfs = (WorkbookFactoryServant *) servant;
	Workbook *workbook;
	
	workbook = workbook_read (filename);
	gtk_widget_show (workbook->toplevel);
	
	if (workbook)
		return workbook->corba_server;
	else
		return CORBA_OBJECT_NIL;
}

static CORBA_boolean
WorkbookFactory_supports (PortableServer_Servant servant,
			  const CORBA_char * obj_goad_id,
			  CORBA_Environment * ev)
{
	g_warning ("Request for: %s\n", obj_goad_id);

	printf ("Getting: %s\n", obj_goad_id);
        if (strcmp (obj_goad_id, "IDL:GNOME:Gnumeric:Workbook:1.0") == 0)
                return CORBA_TRUE;
        else
                return CORBA_FALSE;
}

static CORBA_Object
WorkbookFactory_create_object (PortableServer_Servant servant,
			       const CORBA_char *goad_id,
			       const GNOME_stringlist *params,
			       CORBA_Environment *ev)
{
	Workbook *workbook;

	if (strcmp (goad_id, "IDL:GNOME:Gnumeric:Workbook:1.0") != 0){
                CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
                                     ex_GNOME_GenericFactory_CannotActivate,
                                     NULL);
		return CORBA_OBJECT_NIL;
	}

	workbook = workbook_new ();
	gtk_widget_show (workbook->toplevel);

	return workbook->corba_server;
}

/*
 * Workbook factory server bootstrap
 */
static GNOME_Gnumeric_WorkbookFactory
GNOME_Gnumeric_WorkbookFactory__create (PortableServer_POA poa, CORBA_Environment *ev)
{
	WorkbookFactoryServant *wfs = &workbook_factory_servant;
        PortableServer_ObjectId *objid;

	/*
	 * Set up our tables
	 */
	gnumeric_workbook_factory_epv.read = WorkbookFactory_read;
	gnumeric_workbook_generic_factory_epv.supports = WorkbookFactory_supports;
	gnumeric_workbook_generic_factory_epv.create_object = WorkbookFactory_create_object;
		
	gnumeric_workbook_factory_vepv.GNOME_Gnumeric_WorkbookFactory_epv =
		&gnumeric_workbook_factory_epv;
	gnumeric_workbook_factory_vepv.GNOME_GenericFactory_epv =
		&gnumeric_workbook_generic_factory_epv;
	gnumeric_workbook_factory_vepv._base_epv =
		&gnumeric_workbook_factory_base_epv;

	wfs->servant.vepv = &gnumeric_workbook_factory_vepv;
	POA_GNOME_Gnumeric_WorkbookFactory__init ((PortableServer_Servant) wfs, ev);
	objid = PortableServer_POA_activate_object (poa, wfs, ev);
	CORBA_free (objid);

	return PortableServer_POA_servant_to_reference (poa, wfs, ev);
}

/*
 * CORBA services bootstrap
 */
static gboolean
_WorkbookFactory_init (CORBA_Environment *ev)
{
	PortableServer_POAManager poa_manager;
	int v;

	/*
	 * Get the POA and create the server
	 */
	gnumeric_poa = (PortableServer_POA)
		CORBA_ORB_resolve_initial_references (
		gnome_CORBA_ORB (), "RootPOA", ev);

	if (ev->_major != CORBA_NO_EXCEPTION)
		return FALSE;

	poa_manager = PortableServer_POA__get_the_POAManager (gnumeric_poa, ev);
	if (ev->_major != CORBA_NO_EXCEPTION)
		return FALSE;

	PortableServer_POAManager_activate (poa_manager, ev);
	if (ev->_major != CORBA_NO_EXCEPTION)
		return FALSE;

	/*
	 * Create our workbook factory
	 */
	gnumeric_workbook_factory = GNOME_Gnumeric_WorkbookFactory__create (gnumeric_poa, ev);
	if (ev->_major != CORBA_NO_EXCEPTION)
		return FALSE;

	/* Register the server and see if it was already there */

	v = goad_server_register (CORBA_OBJECT_NIL, gnumeric_workbook_factory,
				  "IDL:GNOME:Gnumeric:WorkbookFactory:1.0", "server", ev);
	if (v == 0)
		return TRUE;

	return FALSE;
}	

gboolean
WorkbookFactory_init (void)
{
	CORBA_Environment ev;
	gboolean retval;
	
	CORBA_exception_init (&ev);
	retval = _WorkbookFactory_init (&ev);
	CORBA_exception_free (&ev);

	return retval;
}
