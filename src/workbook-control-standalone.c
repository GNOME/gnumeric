#include <gnumeric-config.h>
#include "gnumeric.h"
#include "workbook-control-standalone-priv.h"
#include <gsf/gsf-impl-utils.h>

static void
workbook_control_standalone_ctor_class (GObjectClass *object_class)
{
	WorkbookControlGUIClass *wbcg_class
		= WORKBOOK_CONTROL_GUI_CLASS (object_class);

	g_return_if_fail (wbcg_class != NULL);
}

GSF_CLASS (WorkbookControlStandalone, workbook_control_standalone,
	   workbook_control_standalone_ctor_class, NULL,
	   WORKBOOK_CONTROL_GUI_TYPE);
