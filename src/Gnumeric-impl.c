#include "Gnumeric.h"

/*** App-specific servant structures ***/
typedef struct {
   POA_GNOME_Factory servant;
   PortableServer_POA poa;

} impl_POA_GNOME_Factory;

typedef struct {
   POA_GNOME_Table servant;
   PortableServer_POA poa;

} impl_POA_GNOME_Table;

typedef struct {
   POA_GNOME_Spreadsheet servant;
   PortableServer_POA poa;

} impl_POA_GNOME_Spreadsheet;

typedef struct {
   POA_GNOME_Gnumeric servant;
   PortableServer_POA poa;
} impl_POA_GNOME_Gnumeric;

typedef struct {
   POA_GNOME_GnumericFactory servant;
   PortableServer_POA poa;
} impl_POA_GNOME_GnumericFactory;

/*** Implementation stub prototypes ***/
static void impl_GNOME_Factory__destroy(impl_POA_GNOME_Factory * servant,
					CORBA_Environment * ev);

CORBA_Object
impl_GNOME_Factory_new(impl_POA_GNOME_Factory * servant,
		       CORBA_Environment * ev);

CORBA_Object
impl_GNOME_Factory_new_args(impl_POA_GNOME_Factory * servant,
			    CORBA_char * argument,
			    CORBA_Environment * ev);

static void impl_GNOME_Table__destroy(impl_POA_GNOME_Table * servant,
				      CORBA_Environment * ev);

GNOME_Table_Value *
 impl_GNOME_Table_get(impl_POA_GNOME_Table * servant,
		      CORBA_long col,
		      CORBA_long row,
		      CORBA_Environment * ev);

CORBA_long
impl_GNOME_Table_set(impl_POA_GNOME_Table * servant,
		     CORBA_long col,
		     CORBA_long row,
		     GNOME_Table_Value * val,
		     CORBA_Environment * ev);

static void impl_GNOME_Spreadsheet__destroy(impl_POA_GNOME_Spreadsheet * servant,
					    CORBA_Environment * ev);
void
 impl_GNOME_Spreadsheet_set_string(impl_POA_GNOME_Spreadsheet * servant,
				   CORBA_char * text,
				   CORBA_long col,
				   CORBA_long row,
				   CORBA_Environment * ev);

CORBA_char *
 impl_GNOME_Spreadsheet_get_string(impl_POA_GNOME_Spreadsheet * servant,
				   CORBA_long col,
				   CORBA_long row,
				   CORBA_Environment * ev);

GNOME_Table_Value *
 impl_GNOME_Spreadsheet_get(impl_POA_GNOME_Spreadsheet * servant,
			    CORBA_long col,
			    CORBA_long row,
			    CORBA_Environment * ev);

CORBA_long
impl_GNOME_Spreadsheet_set(impl_POA_GNOME_Spreadsheet * servant,
			   CORBA_long col,
			   CORBA_long row,
			   GNOME_Table_Value * val,
			   CORBA_Environment * ev);

static void impl_GNOME_Gnumeric__destroy(impl_POA_GNOME_Gnumeric * servant,
					 CORBA_Environment * ev);
GNOME_Table_Value *
 impl_GNOME_Gnumeric_get(impl_POA_GNOME_Gnumeric * servant,
			 CORBA_long col,
			 CORBA_long row,
			 CORBA_Environment * ev);

CORBA_long
impl_GNOME_Gnumeric_set(impl_POA_GNOME_Gnumeric * servant,
			CORBA_long col,
			CORBA_long row,
			GNOME_Table_Value * val,
			CORBA_Environment * ev);
void
 impl_GNOME_Gnumeric_set_string(impl_POA_GNOME_Gnumeric * servant,
				CORBA_char * text,
				CORBA_long col,
				CORBA_long row,
				CORBA_Environment * ev);
CORBA_char *
 impl_GNOME_Gnumeric_get_string(impl_POA_GNOME_Gnumeric * servant,
				CORBA_long col,
				CORBA_long row,
				CORBA_Environment * ev);

static void impl_GNOME_GnumericFactory__destroy(impl_POA_GNOME_GnumericFactory * servant,
						CORBA_Environment * ev);

CORBA_Object
impl_GNOME_GnumericFactory_new(impl_POA_GNOME_GnumericFactory * servant,
			       CORBA_Environment * ev);
CORBA_Object
impl_GNOME_GnumericFactory_new_args(impl_POA_GNOME_GnumericFactory * servant,
				    CORBA_char * argument,
				    CORBA_Environment * ev);

/*** epv structures ***/
static PortableServer_ServantBase__epv impl_GNOME_Factory_base_epv =
{
   NULL,			/* _private data */
   (gpointer) & impl_GNOME_Factory__destroy,	/* finalize routine */
   NULL,			/* default_POA routine */
};
static POA_GNOME_Factory__epv impl_GNOME_Factory_epv =
{
   NULL,			/* _private */
   (gpointer) & impl_GNOME_Factory_new,

   (gpointer) & impl_GNOME_Factory_new_args,

};

static PortableServer_ServantBase__epv impl_GNOME_Table_base_epv =
{
   NULL,			/* _private data */
   (gpointer) & impl_GNOME_Table__destroy,	/* finalize routine */
   NULL,			/* default_POA routine */
};
static POA_GNOME_Table__epv impl_GNOME_Table_epv =
{
   NULL,			/* _private */

   (gpointer) & impl_GNOME_Table_get,

   (gpointer) & impl_GNOME_Table_set,

};

static PortableServer_ServantBase__epv impl_GNOME_Spreadsheet_base_epv =
{
   NULL,			/* _private data */
   (gpointer) & impl_GNOME_Spreadsheet__destroy,	/* finalize routine */
   NULL,			/* default_POA routine */
};
static POA_GNOME_Spreadsheet__epv impl_GNOME_Spreadsheet_epv =
{
   NULL,			/* _private */
   (gpointer) & impl_GNOME_Spreadsheet_set_string,

   (gpointer) & impl_GNOME_Spreadsheet_get_string,

};
static POA_GNOME_Table__epv impl_GNOME_Spreadsheet_GNOME_Table_epv =
{
   NULL,			/* _private */
   (gpointer) & impl_GNOME_Spreadsheet_get,
   (gpointer) & impl_GNOME_Spreadsheet_set,
};
static PortableServer_ServantBase__epv impl_GNOME_Gnumeric_base_epv =
{
   NULL,			/* _private data */
   (gpointer) & impl_GNOME_Gnumeric__destroy,	/* finalize routine */
   NULL,			/* default_POA routine */
};
static POA_GNOME_Gnumeric__epv impl_GNOME_Gnumeric_epv =
{
   NULL,			/* _private */
};
static POA_GNOME_Table__epv impl_GNOME_Gnumeric_GNOME_Table_epv =
{
   NULL,			/* _private */
   (gpointer) & impl_GNOME_Gnumeric_get,
   (gpointer) & impl_GNOME_Gnumeric_set,
};
static POA_GNOME_Spreadsheet__epv impl_GNOME_Gnumeric_GNOME_Spreadsheet_epv =
{
   NULL,			/* _private */
   (gpointer) & impl_GNOME_Gnumeric_set_string,
   (gpointer) & impl_GNOME_Gnumeric_get_string,
};
static PortableServer_ServantBase__epv impl_GNOME_GnumericFactory_base_epv =
{
   NULL,			/* _private data */
   (gpointer) & impl_GNOME_GnumericFactory__destroy,	/* finalize routine */
   NULL,			/* default_POA routine */
};
static POA_GNOME_GnumericFactory__epv impl_GNOME_GnumericFactory_epv =
{
   NULL,			/* _private */
};
static POA_GNOME_Factory__epv impl_GNOME_GnumericFactory_GNOME_Factory_epv =
{
   NULL,			/* _private */
   (gpointer) & impl_GNOME_GnumericFactory_new,
   (gpointer) & impl_GNOME_GnumericFactory_new_args,
};

/*** vepv structures ***/
static POA_GNOME_Factory__vepv impl_GNOME_Factory_vepv =
{
   &impl_GNOME_Factory_base_epv,
   &impl_GNOME_Factory_epv,
};

static POA_GNOME_Table__vepv impl_GNOME_Table_vepv =
{
   &impl_GNOME_Table_base_epv,
   &impl_GNOME_Table_epv,
};

static POA_GNOME_Spreadsheet__vepv impl_GNOME_Spreadsheet_vepv =
{
   &impl_GNOME_Spreadsheet_base_epv,
   &impl_GNOME_Spreadsheet_GNOME_Table_epv,
   &impl_GNOME_Spreadsheet_epv,
};
static POA_GNOME_Gnumeric__vepv impl_GNOME_Gnumeric_vepv =
{
   &impl_GNOME_Gnumeric_base_epv,
   &impl_GNOME_Gnumeric_GNOME_Table_epv,
   &impl_GNOME_Gnumeric_GNOME_Spreadsheet_epv,
   &impl_GNOME_Gnumeric_epv,
};
static POA_GNOME_GnumericFactory__vepv impl_GNOME_GnumericFactory_vepv =
{
   &impl_GNOME_GnumericFactory_base_epv,
   &impl_GNOME_GnumericFactory_GNOME_Factory_epv,
   &impl_GNOME_GnumericFactory_epv,
};

/*** Stub implementations ***/
static GNOME_Factory 
impl_GNOME_Factory__create(PortableServer_POA poa, CORBA_Environment * ev)
{
   GNOME_Factory retval;
   impl_POA_GNOME_Factory *newservant;
   PortableServer_ObjectId *objid;

   newservant = g_new0(impl_POA_GNOME_Factory, 1);
   newservant->servant.vepv = &impl_GNOME_Factory_vepv;
   newservant->poa = poa;
   POA_GNOME_Factory__init((PortableServer_Servant) newservant, ev);
   objid = PortableServer_POA_activate_object(poa, newservant, ev);
   CORBA_free(objid);
   retval = PortableServer_POA_servant_to_reference(poa, newservant, ev);

   return retval;
}

/* You shouldn't call this routine directly without first deactivating the servant... */
static void
impl_GNOME_Factory__destroy(impl_POA_GNOME_Factory * servant, CORBA_Environment * ev)
{

   POA_GNOME_Factory__fini((PortableServer_Servant) servant, ev);
   g_free(servant);
}

CORBA_Object
impl_GNOME_Factory_new(impl_POA_GNOME_Factory * servant,
		       CORBA_Environment * ev)
{
   CORBA_Object retval;

   return retval;
}

CORBA_Object
impl_GNOME_Factory_new_args(impl_POA_GNOME_Factory * servant,
			    CORBA_char * argument,
			    CORBA_Environment * ev)
{
   CORBA_Object retval;

   return retval;
}

static GNOME_Table 
impl_GNOME_Table__create(PortableServer_POA poa, CORBA_Environment * ev)
{
   GNOME_Table retval;
   impl_POA_GNOME_Table *newservant;
   PortableServer_ObjectId *objid;

   newservant = g_new0(impl_POA_GNOME_Table, 1);
   newservant->servant.vepv = &impl_GNOME_Table_vepv;
   newservant->poa = poa;
   POA_GNOME_Table__init((PortableServer_Servant) newservant, ev);
   objid = PortableServer_POA_activate_object(poa, newservant, ev);
   CORBA_free(objid);
   retval = PortableServer_POA_servant_to_reference(poa, newservant, ev);

   return retval;
}

/* You shouldn't call this routine directly without first deactivating the servant... */
static void
impl_GNOME_Table__destroy(impl_POA_GNOME_Table * servant, CORBA_Environment * ev)
{

   POA_GNOME_Table__fini((PortableServer_Servant) servant, ev);
   g_free(servant);
}

GNOME_Table_Value *
impl_GNOME_Table_get(impl_POA_GNOME_Table * servant,
		     CORBA_long col,
		     CORBA_long row,
		     CORBA_Environment * ev)
{
   GNOME_Table_Value *retval;

   return retval;
}

CORBA_long
impl_GNOME_Table_set(impl_POA_GNOME_Table * servant,
		     CORBA_long col,
		     CORBA_long row,
		     GNOME_Table_Value * val,
		     CORBA_Environment * ev)
{
   CORBA_long retval;

   return retval;
}

static GNOME_Spreadsheet 
impl_GNOME_Spreadsheet__create(PortableServer_POA poa, CORBA_Environment * ev)
{
   GNOME_Spreadsheet retval;
   impl_POA_GNOME_Spreadsheet *newservant;
   PortableServer_ObjectId *objid;

   newservant = g_new0(impl_POA_GNOME_Spreadsheet, 1);
   newservant->servant.vepv = &impl_GNOME_Spreadsheet_vepv;
   newservant->poa = poa;
   POA_GNOME_Spreadsheet__init((PortableServer_Servant) newservant, ev);
   objid = PortableServer_POA_activate_object(poa, newservant, ev);
   CORBA_free(objid);
   retval = PortableServer_POA_servant_to_reference(poa, newservant, ev);

   return retval;
}

/* You shouldn't call this routine directly without first deactivating the servant... */
static void
impl_GNOME_Spreadsheet__destroy(impl_POA_GNOME_Spreadsheet * servant, CORBA_Environment * ev)
{

   POA_GNOME_Spreadsheet__fini((PortableServer_Servant) servant, ev);
   g_free(servant);
}

void
impl_GNOME_Spreadsheet_set_string(impl_POA_GNOME_Spreadsheet * servant,
				  CORBA_char * text,
				  CORBA_long col,
				  CORBA_long row,
				  CORBA_Environment * ev)
{
}

CORBA_char *
impl_GNOME_Spreadsheet_get_string(impl_POA_GNOME_Spreadsheet * servant,
				  CORBA_long col,
				  CORBA_long row,
				  CORBA_Environment * ev)
{
   CORBA_char *retval;

   return retval;
}

GNOME_Table_Value *
impl_GNOME_Spreadsheet_get(impl_POA_GNOME_Spreadsheet * servant,
			   CORBA_long col,
			   CORBA_long row,
			   CORBA_Environment * ev)
{
   GNOME_Table_Value *retval;

   return retval;
}

CORBA_long
impl_GNOME_Spreadsheet_set(impl_POA_GNOME_Spreadsheet * servant,
			   CORBA_long col,
			   CORBA_long row,
			   GNOME_Table_Value * val,
			   CORBA_Environment * ev)
{
   CORBA_long retval;

   return retval;
}

static GNOME_Gnumeric 
impl_GNOME_Gnumeric__create(PortableServer_POA poa, CORBA_Environment * ev)
{
   GNOME_Gnumeric retval;
   impl_POA_GNOME_Gnumeric *newservant;
   PortableServer_ObjectId *objid;

   newservant = g_new0(impl_POA_GNOME_Gnumeric, 1);
   newservant->servant.vepv = &impl_GNOME_Gnumeric_vepv;
   newservant->poa = poa;
   POA_GNOME_Gnumeric__init((PortableServer_Servant) newservant, ev);
   objid = PortableServer_POA_activate_object(poa, newservant, ev);
   CORBA_free(objid);
   retval = PortableServer_POA_servant_to_reference(poa, newservant, ev);

   return retval;
}

/* You shouldn't call this routine directly without first deactivating the servant... */
static void
impl_GNOME_Gnumeric__destroy(impl_POA_GNOME_Gnumeric * servant, CORBA_Environment * ev)
{

   POA_GNOME_Gnumeric__fini((PortableServer_Servant) servant, ev);
   g_free(servant);
}

GNOME_Table_Value *
impl_GNOME_Gnumeric_get(impl_POA_GNOME_Gnumeric * servant,
			CORBA_long col,
			CORBA_long row,
			CORBA_Environment * ev)
{
   GNOME_Table_Value *retval;

   return retval;
}

CORBA_long
impl_GNOME_Gnumeric_set(impl_POA_GNOME_Gnumeric * servant,
			CORBA_long col,
			CORBA_long row,
			GNOME_Table_Value * val,
			CORBA_Environment * ev)
{
   CORBA_long retval;

   return retval;
}

void
impl_GNOME_Gnumeric_set_string(impl_POA_GNOME_Gnumeric * servant,
			       CORBA_char * text,
			       CORBA_long col,
			       CORBA_long row,
			       CORBA_Environment * ev)
{
}

CORBA_char *
impl_GNOME_Gnumeric_get_string(impl_POA_GNOME_Gnumeric * servant,
			       CORBA_long col,
			       CORBA_long row,
			       CORBA_Environment * ev)
{
   CORBA_char *retval;

   return retval;
}

static GNOME_GnumericFactory 
impl_GNOME_GnumericFactory__create(PortableServer_POA poa, CORBA_Environment * ev)
{
   GNOME_GnumericFactory retval;
   impl_POA_GNOME_GnumericFactory *newservant;
   PortableServer_ObjectId *objid;

   newservant = g_new0(impl_POA_GNOME_GnumericFactory, 1);
   newservant->servant.vepv = &impl_GNOME_GnumericFactory_vepv;
   newservant->poa = poa;
   POA_GNOME_GnumericFactory__init((PortableServer_Servant) newservant, ev);
   objid = PortableServer_POA_activate_object(poa, newservant, ev);
   CORBA_free(objid);
   retval = PortableServer_POA_servant_to_reference(poa, newservant, ev);

   return retval;
}

/* You shouldn't call this routine directly without first deactivating the servant... */
static void
impl_GNOME_GnumericFactory__destroy(impl_POA_GNOME_GnumericFactory * servant, CORBA_Environment * ev)
{

   POA_GNOME_GnumericFactory__fini((PortableServer_Servant) servant, ev);
   g_free(servant);
}

CORBA_Object
impl_GNOME_GnumericFactory_new (impl_POA_GNOME_GnumericFactory * servant,
				CORBA_Environment * ev)
{
	CORBA_Object retval;

	return retval;
}

CORBA_Object
impl_GNOME_GnumericFactory_new_args(impl_POA_GNOME_GnumericFactory * servant,
				    CORBA_char * argument,
				    CORBA_Environment * ev)
{
   CORBA_Object retval;

   return retval;
}
