#ifndef GNUMERIC_GCONF_PRIV_H
#define GNUMERIC_GCONF_PRIV_H

/*
 *  Note: This file must stay synchronized with the corresponding schema file!
 *
 *        This file should only be included into gnumeric-gconf.c  and
 *        dialogs/dialog-preferences.c
 */


/*
 *  schemas/gnumeric-dialogs.schemas
 */
#define FUNCTION_SELECT_GCONF_RECENT "/apps/gnumeric/functionselector/recentfunctions"
#define FUNCTION_SELECT_GCONF_NUM_OF_RECENT "/apps/gnumeric/functionselector/num-of-recent"

#define AUTOCORRECT_DIRECTORY "/apps/gnumeric/autocorrect"
#define AUTOCORRECT_INIT_CAPS AUTOCORRECT_DIRECTORY "/init-caps"
#define AUTOCORRECT_INIT_CAPS_LIST AUTOCORRECT_DIRECTORY "/init-caps-list"
#define AUTOCORRECT_FIRST_LETTER AUTOCORRECT_DIRECTORY "/first-letter"
#define AUTOCORRECT_FIRST_LETTER_LIST AUTOCORRECT_DIRECTORY "/first-letter-list"
#define AUTOCORRECT_NAMES_OF_DAYS AUTOCORRECT_DIRECTORY "/names-of-days"
#define AUTOCORRECT_REPLACE AUTOCORRECT_DIRECTORY "/replace"

#define PLUGIN_GCONF_DIRECTORY "/apps/gnumeric/plugins"
#define PLUGIN_GCONF_ACTIVATE_NEW PLUGIN_GCONF_DIRECTORY "/activate-new"
#define PLUGIN_GCONF_ACTIVE PLUGIN_GCONF_DIRECTORY "/active"
#define PLUGIN_GCONF_KNOWN PLUGIN_GCONF_DIRECTORY "/known"
#define PLUGIN_GCONF_FILE_STATES PLUGIN_GCONF_DIRECTORY "/file-states"
#define PLUGIN_GCONF_EXTRA_DIRS PLUGIN_GCONF_DIRECTORY "/extra-dirs"

#define AUTOFORMAT_GCONF_DIRECTORY "/apps/gnumeric/autoformat"
#define AUTOFORMAT_GCONF_EXTRA_DIRS AUTOFORMAT_GCONF_DIRECTORY "/extra-dirs"
#define AUTOFORMAT_GCONF_SYS_DIR AUTOFORMAT_GCONF_DIRECTORY "/sys-dir"
#define AUTOFORMAT_GCONF_USR_DIR AUTOFORMAT_GCONF_DIRECTORY "/usr-dir"

/*
 *  schemas/gnumeric-general.schemas
 */

#define GNUMERIC_GCONF_UNDO_DIRECTORY "/apps/gnumeric/undo"
#define GNUMERIC_GCONF_UNDO_SIZE GNUMERIC_GCONF_UNDO_DIRECTORY "/size"
#define GNUMERIC_GCONF_UNDO_MAXNUM GNUMERIC_GCONF_UNDO_DIRECTORY "/maxnum"
#define GNUMERIC_GCONF_UNDO_SHOW_SHEET_NAME GNUMERIC_GCONF_UNDO_DIRECTORY "/show_sheet_name"
#define GNUMERIC_GCONF_UNDO_MAX_DESCRIPTOR_WIDTH GNUMERIC_GCONF_UNDO_DIRECTORY "/max_descriptor_width"

#define GNUMERIC_GCONF_FONT_DIRECTORY "/apps/gnumeric/core/defaultfont"
#define GNUMERIC_GCONF_FONT_NAME GNUMERIC_GCONF_FONT_DIRECTORY "/name"
#define GNUMERIC_GCONF_FONT_SIZE GNUMERIC_GCONF_FONT_DIRECTORY "/size"
#define GNUMERIC_GCONF_FONT_BOLD GNUMERIC_GCONF_FONT_DIRECTORY "/bold"
#define GNUMERIC_GCONF_FONT_ITALIC GNUMERIC_GCONF_FONT_DIRECTORY "/italic"

#define GNUMERIC_GCONF_FILE_HISTORY_N "/apps/gnumeric/core/file/history/n"
#define GNUMERIC_GCONF_FILE_HISTORY_FILES "/apps/gnumeric/core/file/history/files"

#define GNUMERIC_GCONF_WORKBOOK_NSHEETS "/apps/gnumeric/core/workbook/n-sheet"

#define GNUMERIC_GCONF_GUI_DIRECTORY "/apps/gnumeric/core/gui"
#define GNUMERIC_GCONF_GUI_RES_H GNUMERIC_GCONF_GUI_DIRECTORY "/screen/horizontaldpi"
#define GNUMERIC_GCONF_GUI_RES_V GNUMERIC_GCONF_GUI_DIRECTORY "/screen/verticaldpi"
#define GNUMERIC_GCONF_GUI_ED_AUTOCOMPLETE GNUMERIC_GCONF_GUI_DIRECTORY "/editing/autocomplete"
#define GNUMERIC_GCONF_GUI_ED_LIVESCROLLING GNUMERIC_GCONF_GUI_DIRECTORY "/editing/livescrolling"
#define GNUMERIC_GCONF_GUI_ED_RECALC_LAG GNUMERIC_GCONF_GUI_DIRECTORY "/editing/recalclag"
#define GNUMERIC_GCONF_GUI_WINDOW_X GNUMERIC_GCONF_GUI_DIRECTORY "/window/x"
#define GNUMERIC_GCONF_GUI_WINDOW_Y GNUMERIC_GCONF_GUI_DIRECTORY "/window/y"




#include <gconf/gconf-client.h>

#endif /* GNUMERIC_GRAPH_H */
