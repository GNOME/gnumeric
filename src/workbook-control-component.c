#include <gnumeric-config.h>
#include "gnumeric.h"
#include "workbook-control-component-priv.h"
#include <gal/util/e-util.h>

static void
workbook_control_component_ctor_class (GObjectClass *object_class)
{
	WorkbookControlGUIClass *wbcg_class
		= WORKBOOK_CONTROL_GUI_CLASS (object_class);

	g_return_if_fail (wbcg_class != NULL);
}

E_MAKE_TYPE(workbook_control_component, "WorkbookControlComponent",
	    WorkbookControlComponent,
	    workbook_control_component_ctor_class, NULL,
	    WORKBOOK_CONTROL_GUI_TYPE);
