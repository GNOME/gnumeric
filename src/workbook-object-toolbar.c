
#include "sheet.h"
#include "sheet-control-gui.h"
#include "workbook-control-gui-priv.h"
#include "workbook.h"
#include "sheet-object-widget.h"
#include "sheet-object-graphic.h"
#include "sheet-object-graph.h"
#include "gui-util.h"

#include <gtk/gtkactiongroup.h>
#ifdef WITH_BONOBO
#include <bonobo.h>
#endif

#else

void
workbook_create_object_toolbar (WorkbookControlGUI *wbcg)
{
	bonobo_ui_component_add_verb_list_with_data (wbcg->uic, verbs, wbcg);
}
#endif
