#ifndef GNUMERIC_GCONF_H
#define GNUMERIC_GCONF_H

/*
 *  Note: This file must stay synchronized with the corresponding schema file!
 *
 *
 */


/*
 *  schemas/gnumeric-dialogs.schemas
 */

#define GNUMERIC_GCONF_UNDO_DIRECTORY "/apps/gnumeric/undo"
#define GNUMERIC_GCONF_UNDO_SIZE GNUMERIC_GCONF_UNDO_DIRECTORY "/size"
#define GNUMERIC_GCONF_UNDO_MAXNUM GNUMERIC_GCONF_UNDO_DIRECTORY "/maxnum"
#define GNUMERIC_GCONF_UNDO_SHOW_SHEET_NAME GNUMERIC_GCONF_UNDO_DIRECTORY "/show_sheet_name"
#define GNUMERIC_GCONF_UNDO_MAX_DESCRIPTOR_WIDTH GNUMERIC_GCONF_UNDO_DIRECTORY "/max_descriptor_width"

#define AUTOCORRECT_DIRECTORY "/apps/gnumeric/autocorrect"
#define AUTOCORRECT_INIT_CAPS AUTOCORRECT_DIRECTORY "/init-caps"
#define AUTOCORRECT_INIT_CAPS_LIST AUTOCORRECT_DIRECTORY "/init-caps-list"
#define AUTOCORRECT_FIRST_LETTER AUTOCORRECT_DIRECTORY "/first-letter"
#define AUTOCORRECT_FIRST_LETTER_LIST AUTOCORRECT_DIRECTORY "/first-letter-list"
#define AUTOCORRECT_NAMES_OF_DAYS AUTOCORRECT_DIRECTORY "/names-of-days"
#define AUTOCORRECT_REPLACE AUTOCORRECT_DIRECTORY "/replace"


/*
 *  schemas/gnumeric-general.schemas
 */

#define GNUMERIC_GCONF_FONT_DIRECTORY "/apps/gnumeric/core/defaultfont"
#define GNUMERIC_GCONF_FONT_NAME GNUMERIC_GCONF_FONT_DIRECTORY "/name"
#define GNUMERIC_GCONF_FONT_SIZE GNUMERIC_GCONF_FONT_DIRECTORY "/size"
#define GNUMERIC_GCONF_FONT_BOLD GNUMERIC_GCONF_FONT_DIRECTORY "/bold"
#define GNUMERIC_GCONF_FONT_ITALIC GNUMERIC_GCONF_FONT_DIRECTORY "/italic"

#define GNUMERIC_GCONF_FILE_HISTORY_N "/apps/gnumeric/core/file/history/n"
#define GNUMERIC_GCONF_FILE_HISTORY_FILES "/apps/gnumeric/core/file/history/files"

#define GNUMERIC_GCONF_WORKBOOK_NSHEETS "/apps/gnumeric/core/workbook/n-sheet"

#define GNUMERIC_GCONF_GUI_RES_H "/apps/gnumeric/core/gui/screen/horizontaldpi"
#define GNUMERIC_GCONF_GUI_RES_V "/apps/gnumeric/core/gui/screen/verticaldpi"
#define GNUMERIC_GCONF_GUI_ED_AUTOCOMPLETE "/apps/gnumeric/core/gui/editing/autocomplete"
#define GNUMERIC_GCONF_GUI_ED_LIVESCROLLING "/apps/gnumeric/core/gui/editing/livescrolling"
#define GNUMERIC_GCONF_GUI_ED_RECALC_LAG "/apps/gnumeric/core/gui/editing/recalclag"


#define FUNCTION_SELECT_GCONF_RECENT "/apps/gnumeric/functionselector/recentfunctions"
#define FUNCTION_SELECT_GCONF_NUM_OF_RECENT "/apps/gnumeric/functionselector/num-of-recent"



#include <gconf/gconf-client.h>

#endif /* GNUMERIC_GRAPH_H */
