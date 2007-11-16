/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_GCONF_PRIV_H_
# define _GNM_GCONF_PRIV_H_

G_BEGIN_DECLS

/*
 *  Note: This file must stay synchronized with the corresponding schema file!
 *
 *        This file should only be included into gnumeric-gconf.c  and
 *        dialogs/dialog-preferences.c
 */

#define GNM_CONF_DIR			"gnumeric"

/*
 *  schemas/gnumeric-dialogs.schemas
 */
#define FUNCTION_SELECT_GCONF_DIR		"functionselector"
#define FUNCTION_SELECT_GCONF_RECENT		"recentfunctions"
#define FUNCTION_SELECT_GCONF_NUM_OF_RECENT	"num-of-recent"

#define CONF_DEFAULT_FONT_DIR	  "core/defaultfont"
#define CONF_DEFAULT_FONT_NAME	  "name"
#define CONF_DEFAULT_FONT_SIZE	  "size"
#define CONF_DEFAULT_FONT_BOLD	  "bold"
#define CONF_DEFAULT_FONT_ITALIC  "italic"

#define PLUGIN_GCONF_DIR		"plugins"
#define PLUGIN_GCONF_ACTIVATE_NEW	"activate-new"
#define PLUGIN_GCONF_ACTIVE		"active"
#define PLUGIN_GCONF_FILE_STATES	"file-states"
#define PLUGIN_GCONF_EXTRA_DIRS		"extra-dirs"

#define AUTOFORMAT_GCONF_DIR		"autoformat"
#define AUTOFORMAT_GCONF_EXTRA_DIRS	"extra-dirs"
#define AUTOFORMAT_GCONF_SYS_DIR	"sys-dir"
#define AUTOFORMAT_GCONF_USR_DIR	"usr-dir"

#define PRINTSETUP_GCONF_DIR			"printsetup"
#define PRINTSETUP_GCONF_ALL_SHEETS		"all-sheets"
#define PRINTSETUP_GCONF_HEADER			"header"
#define PRINTSETUP_GCONF_FOOTER			"footer"
#define PRINTSETUP_GCONF_HF_FONT_NAME		"hf-font-name"
#define PRINTSETUP_GCONF_HF_FONT_SIZE		"hf-font-size"
#define PRINTSETUP_GCONF_HF_FONT_BOLD		"hf-font-bold"
#define PRINTSETUP_GCONF_HF_FONT_ITALIC		"hf-font-italic"
#define PRINTSETUP_GCONF_CENTER_HORIZONTALLY	"center-horizontally"
#define PRINTSETUP_GCONF_CENTER_VERTICALLY	"center-vertically"
#define PRINTSETUP_GCONF_PRINT_GRID_LINES	"print-grid-lines"
#define PRINTSETUP_GCONF_EVEN_IF_ONLY_STYLES	"print-even-if-only-styles"
#define PRINTSETUP_GCONF_PRINT_BLACK_AND_WHITE	"print-black-n-white"
#define PRINTSETUP_GCONF_PRINT_TITLES		"print-titles"
#define PRINTSETUP_GCONF_ACROSS_THEN_DOWN	"across-then-down"
#define PRINTSETUP_GCONF_SCALE_PERCENTAGE	"scale-percentage"
#define PRINTSETUP_GCONF_SCALE_PERCENTAGE_VALUE	"scale-percentage-value"
#define PRINTSETUP_GCONF_SCALE_WIDTH		"scale-width"
#define PRINTSETUP_GCONF_SCALE_HEIGHT		"scale-height"
#define PRINTSETUP_GCONF_REPEAT_TOP		"repeat-top"
#define PRINTSETUP_GCONF_REPEAT_LEFT		"repeat-left"
#define PRINTSETUP_GCONF_MARGIN_TOP		"margin-top"
#define PRINTSETUP_GCONF_MARGIN_BOTTOM		"margin-bottom"
#define PRINTSETUP_GCONF_MARGIN_GTK_TOP		"margin-gtk-top"
#define PRINTSETUP_GCONF_MARGIN_GTK_BOTTOM	"margin-gtk-bottom"
#define PRINTSETUP_GCONF_MARGIN_GTK_LEFT	"margin-gtk-left"
#define PRINTSETUP_GCONF_MARGIN_GTK_RIGHT	"margin-gtk-right"
#define PRINTSETUP_GCONF_PAPER          	"paper"
#define PRINTSETUP_GCONF_PAPER_ORIENTATION     	"paper-orientation"
#define PRINTSETUP_GCONF_PREFERRED_UNIT		"preferred-unit"
#define PRINTSETUP_GCONF_HEADER_FORMAT_LEFT	"hf-left"
#define PRINTSETUP_GCONF_HEADER_FORMAT_MIDDLE	"hf-middle"
#define PRINTSETUP_GCONF_HEADER_FORMAT_RIGHT	"hf-right"
#define PRINTSETUP_GCONF_GTKSETTING	        "gtk-setting"

#define DIALOGS_GCONF_DIR		"dialogs"
#define DIALOGS_GCONF_UNFOCUSED_RS	"rs/unfocused"

/*
 *  schemas/gnumeric-general.schemas
 */

#define GNM_CONF_UNDO_DIR			"undo"
#define GNM_CONF_UNDO_SIZE			"size"
#define GNM_CONF_UNDO_MAXNUM			"maxnum"
#define GNM_CONF_UNDO_SHOW_SHEET_NAME		"show_sheet_name"
#define GNM_CONF_UNDO_MAX_DESCRIPTOR_WIDTH	"max_descriptor_width"

#define GNM_CONF_FONT_DIR		"core/defaultfont"
#define GNM_CONF_FONT_NAME		"name"
#define GNM_CONF_FONT_SIZE		"size"
#define GNM_CONF_FONT_BOLD		"bold"
#define GNM_CONF_FONT_ITALIC		"italic"

#define GNM_CONF_FILE_DIR		"core/file"
#define GNM_CONF_FILE_HISTORY_N		"history/n"
#define GNM_CONF_FILE_HISTORY_FILES	"history/files"
#define GNM_CONF_FILE_OVERWRITE_DEFAULT	"save/def-overwrite"
#define GNM_CONF_FILE_SINGLE_SHEET_SAVE	"save/single_sheet"

#define GNM_CONF_WORKBOOK_NSHEETS	"core/workbook/n-sheet"

#define GNM_CONF_GUI_DIR		"core/gui"
#define GNM_CONF_GUI_RES_H		"screen/horizontaldpi"
#define GNM_CONF_GUI_RES_V		"screen/verticaldpi"
#define GNM_CONF_GUI_ED_AUTOCOMPLETE	"editing/autocomplete"
#define GNM_CONF_GUI_ED_ENTER_MOVES_DIR	"editing/enter_moves_dir"
#define GNM_CONF_GUI_ED_TRANSITION_KEYS	"editing/transitionkeys"
#define GNM_CONF_GUI_ED_LIVESCROLLING	"editing/livescrolling"
#define GNM_CONF_GUI_ED_RECALC_LAG	"editing/recalclag"
#define GNM_CONF_GUI_WINDOW_X		"window/x"
#define GNM_CONF_GUI_WINDOW_Y		"window/y"
#define GNM_CONF_GUI_ZOOM		"window/zoom"
#define GNM_CONF_GUI_TOOLBARS		"toolbars"

#define GNM_CONF_XML_COMPRESSION	"core/xml/compression-level"

#define GNM_CONF_SORT_DIR			"core/sort"
#define GNM_CONF_SORT_DEFAULT_HAS_HEADER       	"default/has-header"
#define GNM_CONF_SORT_DEFAULT_BY_CASE		"default/by-case"
#define GNM_CONF_SORT_DEFAULT_RETAIN_FORM	"default/retain-formats"
#define GNM_CONF_SORT_DEFAULT_ASCENDING		"default/ascending"
#define GNM_CONF_SORT_DIALOG_MAX_INITIAL	"dialog/max-initial-clauses"

#define GNM_CONF_CUTANDPASTE_DIR		"cut-and-paste"
#define GNM_CONF_CUTANDPASTE_PREFER_CLIPBOARD	"prefer-clipboard"

/*
 *  schemas/gnumeric-plugins.schemas
 */

#define PLUGIN_GCONF_LATEX		"plugin/latex"
#define PLUGIN_GCONF_LATEX_USE_UTF8	"use-utf8"

G_END_DECLS

#endif /* _GNM_GCONF_PRIV_H_ */
