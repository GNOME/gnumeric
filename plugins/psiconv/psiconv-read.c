/* vim: set sw=8: */

/*
 * psiconv-read.c : Routines to read Psion 5 series Sheet files
 *
 * Copyright (C) 2001 Frodo Looijaard (frodol@dds.nl)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

/* Search for `TODO' for a list of things to do */

/* TODO: Limit the number of include files a bit */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <libgnome/libgnome.h>
#include "psiconv-plugin.h"
#include "application.h"
#include "expr.h"
#include "value.h"
#include "sheet.h"
#include "number-match.h"
#include "cell.h"
#include "parse-util.h"
#include "sheet-style.h"
#include "style.h"
#include "style-border.h"
#include "style-color.h"
#include "selection.h"
#include "position.h"
#include "ranges.h"
#include "io-context.h"
#include "workbook-view.h"
#include "workbook.h"
#include "error-info.h"

#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#include <psiconv/parse.h>

static void
append_zeros (char *s, int n)
{
	if (n > 0) {
		s = s + strlen (s);
		*s++ = '.';
		while (n--)
			*s++ = '0';
		*s = 0;
	}
}


static CellRef *
cellref_init (CellRef *res,
	      int row, gboolean row_abs,
	      int col, gboolean col_abs)
{
	res->sheet = NULL;
	res->row = row;
	res->col = col;
	res->row_relative = row_abs ? 0 : 1;
	res->col_relative = col_abs ? 0 : 1;
	return res;
}

static void
set_format(MStyle *style, const psiconv_sheet_numberformat psi_numberformat)
{
	/* 100 should be long enough, but to be really safe, use strncpy */
	char fmt_string[100];

	/* TODO: Dates and times are still wrong. What about localisation? */
	strcpy(fmt_string,"");
        if (psi_numberformat->code == psiconv_numberformat_fixeddecimal) {
		strcpy(fmt_string,"0");
		append_zeros(fmt_string, psi_numberformat->decimal);
        } else if (psi_numberformat->code == psiconv_numberformat_scientific) {
		strcpy(fmt_string,"0");
		append_zeros(fmt_string, psi_numberformat->decimal);
		strcat (fmt_string, "E+00");
        } else if (psi_numberformat->code == psiconv_numberformat_currency) {
		/* TODO: Determine currency symbol somehow */
		strcpy(fmt_string,"$0");
		append_zeros(fmt_string, psi_numberformat->decimal);
	} else if (psi_numberformat->code == psiconv_numberformat_percent) {
		strcpy(fmt_string,"0");
		append_zeros(fmt_string, psi_numberformat->decimal);
		strcat (fmt_string, "%");
	} else if (psi_numberformat->code == psiconv_numberformat_triads) {
		strcpy(fmt_string,"#,##0");
		append_zeros(fmt_string, psi_numberformat->decimal);
	} else if (psi_numberformat->code == psiconv_numberformat_text) {
		strcpy(fmt_string,"@");
	} else if (psi_numberformat->code == psiconv_numberformat_date_dmm) {
		strcpy(fmt_string,"d-mm");
	} else if (psi_numberformat->code == psiconv_numberformat_date_mmd) {
		strcpy(fmt_string,"mm-d");
	} else if (psi_numberformat->code == psiconv_numberformat_date_ddmmyy) {
		strcpy(fmt_string,"dd-mm-yy");
	} else if (psi_numberformat->code == psiconv_numberformat_date_mmddyy) {
		strcpy(fmt_string,"mm-dd-yy");
	} else if (psi_numberformat->code == psiconv_numberformat_date_yymmdd) {
		strcpy(fmt_string,"yy-mm-dd");
	} else if (psi_numberformat->code == psiconv_numberformat_date_dmmm) {
		strcpy(fmt_string,"d mmm");
	} else if (psi_numberformat->code == psiconv_numberformat_date_dmmmyy) {
		strcpy(fmt_string,"d mmm yy");
	} else if (psi_numberformat->code ==
                   psiconv_numberformat_date_ddmmmyy) {
		strcpy(fmt_string,"dd mmm yy");
	} else if (psi_numberformat->code ==
                   psiconv_numberformat_date_mmm) { strcpy(fmt_string,"mmm");
	} else if (psi_numberformat->code ==
                   psiconv_numberformat_date_monthname) {
		strcpy(fmt_string,"mmmm");
	} else if (psi_numberformat->code ==
                   psiconv_numberformat_date_mmmyy) {
		strcpy(fmt_string,"mmm yy");
	} else if (psi_numberformat->code ==
                   psiconv_numberformat_date_monthnameyy) {
		strcpy(fmt_string,"mmmm yy");
	} else if (psi_numberformat->code ==
                   psiconv_numberformat_date_monthnamedyyyy) {
		strcpy(fmt_string,"mmmm d, yyyy");
	} else if (psi_numberformat->code ==
                   psiconv_numberformat_datetime_ddmmyyyyhhii) {
		strcpy(fmt_string,"dd-mm-yyyy h:mm AM/PM");
	} else if (psi_numberformat->code ==
                   psiconv_numberformat_datetime_ddmmyyyyHHii) {
		strcpy(fmt_string,"dd-mm-yyyy h:mm");
	} else if (psi_numberformat->code ==
                   psiconv_numberformat_datetime_mmddyyyyhhii) {
		strcpy(fmt_string,"mm-dd-yyyy h:mm AM/PM");
	} else if (psi_numberformat->code ==
                   psiconv_numberformat_datetime_mmddyyyyHHii) {
		strcpy(fmt_string,"mm-dd-yyyy h:mm");
	} else if (psi_numberformat->code ==
                   psiconv_numberformat_datetime_yyyymmddhhii) {
		strcpy(fmt_string,"yyyy-mm-dd h:mm AM/PM");
	} else if (psi_numberformat->code ==
                   psiconv_numberformat_datetime_yyyymmddHHii) {
		strcpy(fmt_string,"yyyy-mm-dd h:mm");
	} else if (psi_numberformat->code == psiconv_numberformat_time_hhii) {
		strcpy(fmt_string,"h:mm AM/PM");
	} else if (psi_numberformat->code == psiconv_numberformat_time_hhiiss) {
		strcpy(fmt_string,"h:mm:ss AM/PM");
	} else if (psi_numberformat->code == psiconv_numberformat_time_HHii) {
		strcpy(fmt_string,"h:mm");
	} else if (psi_numberformat->code == psiconv_numberformat_time_HHiiss) {
		strcpy(fmt_string,"h:mm:ss");
	}  /* TODO: Add True/False */

	if (fmt_string[0])
		mstyle_set_format_text (style, fmt_string);
}

static StyleColor *
get_color(const psiconv_color color)
{
	return style_color_new(color->red*256,color->green*256,color->blue*256);
}

static void
set_layout(MStyle * style,const psiconv_sheet_cell_layout psi_layout)
{
	StyleColor *color;

	set_format(style,psi_layout->numberformat);
	mstyle_set_font_size(style,psi_layout->character->font_size);
	mstyle_set_font_italic(style,psi_layout->character->italic?TRUE:FALSE);
	mstyle_set_font_bold(style,psi_layout->character->bold?TRUE:FALSE);
	mstyle_set_font_uline(style,
	                      psi_layout->character->underline?TRUE:FALSE);
	mstyle_set_font_strike(style,
	                       psi_layout->character->strikethrough?TRUE:FALSE);
	mstyle_set_font_name(style,psi_layout->character->font->name);
	color = get_color(psi_layout->character->color);
	if (color)
		mstyle_set_color(style,MSTYLE_COLOR_FORE,color);
	/* TODO: Character level layouts: super_sub */
	/* TODO: Paragraph level layouts: all */
	/* TODO: Background color: add transparant if white */
#if 0
	color = get_color(psi_layout->paragraph->back_color);
	if (color) {
		mstyle_set_color(style,MSTYLE_COLOR_BACK,color);
		mstyle_set_color(style,MSTYLE_COLOR_PATTERN,color);
		/* TODO: Replace 24 with some symbol */
		mstyle_set_pattern(style,1);
	}
#endif
}


static void
set_style(Sheet *sheet, int row, int col,
          const psiconv_sheet_cell_layout psi_layout,
          const MStyle *default_style)
{
	MStyle *style;
	style = mstyle_copy(default_style);
	if (!style)
		return;
	set_layout(style,psi_layout);
	sheet_style_set_pos(sheet,col,row,style);
}

static Value *
value_new_from_psi_cell(const psiconv_sheet_cell psi_cell)
{
	switch (psi_cell->type) {
	case psiconv_cell_int :
		return value_new_int(psi_cell->data.dat_int);
	case psiconv_cell_float :
		return value_new_float(psi_cell->data.dat_float);
	case psiconv_cell_string :
		return value_new_string(psi_cell->data.dat_string);
	case psiconv_cell_bool :
		return value_new_bool(psi_cell->data.dat_bool);
	case psiconv_cell_blank :
		return value_new_empty();
	case psiconv_cell_error :
		/* TODO: value_new_error */
		return value_new_empty();
	default :
		/* TODO: value_new_error */
		return value_new_empty();
	}
	return NULL;
}

static GnmExpr *
parse_subexpr(const psiconv_formula psi_formula)
{
	int nrargs=0; /* -1 for variable */
	int kind=-1; /* 0 for dat, 1 for operator, 2 for formula, 3 for special,
	               -1 for unknown */
	psiconv_formula psi_form1,psi_form2;
	GnmExpr *expr1=NULL,*expr2=NULL;
	CellRef *cr1=NULL,*cr2=NULL;

	switch(psi_formula->type) {
		/* Translates values */
		case psiconv_formula_dat_float:
		case psiconv_formula_dat_int:
		case psiconv_formula_dat_string:
		case psiconv_formula_dat_cellblock:
		case psiconv_formula_dat_vcellblock:
			nrargs = 0;
			kind = 0;
			break;
		/* Translates to binary operators */
		case psiconv_formula_op_lt:
		case psiconv_formula_op_le:
		case psiconv_formula_op_gt:
		case psiconv_formula_op_ge:
		case psiconv_formula_op_ne:
		case psiconv_formula_op_eq:
		case psiconv_formula_op_add:
		case psiconv_formula_op_sub:
		case psiconv_formula_op_mul:
		case psiconv_formula_op_div:
	/*	case psiconv_formula_op_pow: */
	/*	case psiconv_formula_op_and: */
	/*	case psiconv_formula_op_or:  */
	/*	case psiconv_formula_op_con: */
			nrargs = 2;
			kind = 1;
			break;
		/* Translates to unary operators */
		case psiconv_formula_op_pos:
		case psiconv_formula_op_neg:
		case psiconv_formula_op_not:
			nrargs = 1;
			kind = 1;
			break;
		/* Specially handled */
		case psiconv_formula_dat_cellref:
		case psiconv_formula_op_bra:
			nrargs = 1;
			kind = 3;
			break;
		/* Should never happen; caught by the default */
	/*	case psiconv_formula_mark_eof:   */
	/*	case psiconv_formula_mark_opsep: */
	/*	case psiconv_formula_mark_opend: */
		default:
	/*		kind = -1; */
	/*		break;     */
	}

	if (kind == -1) {
		/* Unknown value */
		return NULL;
	} else if (kind == 0) {
		/* Handling data */
		Value *v = NULL;
		switch(psi_formula->type) {
		case psiconv_formula_dat_float:
			v = value_new_float(psi_formula->data.dat_float);
			break;
		case psiconv_formula_dat_int:
			v = value_new_int(psi_formula->data.dat_int);
			break;
		case psiconv_formula_dat_string:
			v = value_new_string(psi_formula->data.dat_string);
			break;
		case psiconv_formula_dat_cellblock: {
			CellRef cr1, cr2;

			cellref_init (&cr1,
				psi_formula->data.dat_cellblock.first.row.offset,
				psi_formula->data.dat_cellblock.first.row.absolute,
				psi_formula->data.dat_cellblock.first.column.offset,
				psi_formula->data.dat_cellblock.first.column.absolute);
			cellref_init (&cr2,
				psi_formula->data.dat_cellblock.last.row.offset,
				psi_formula->data.dat_cellblock.last.row.absolute,
				psi_formula->data.dat_cellblock.last.column.offset,
				psi_formula->data.dat_cellblock.last.column.absolute);

			v = value_new_cellrange (&cr1, &cr2, 1, 1);
			break;
		}
		default:
		}
		if (!v)
			return NULL;
		return gnm_expr_new_constant(v);
	} else if (kind == 1) {
		/* Handling the operators */
		if (nrargs >= 1) {
			if (!(psi_form1 = psiconv_list_get
			                  (psi_formula->data.fun_operands,0)))
				return NULL;
			if (!(expr1 = parse_subexpr(psi_form1)))
				return NULL;
		}
		if (nrargs >= 2) {
			if (!(psi_form2 = psiconv_list_get
			                  (psi_formula->data.fun_operands,1))) {
				gnm_expr_unref(expr1);
				return NULL;
			}
			if (!(expr2 = parse_subexpr(psi_form2))) {
				gnm_expr_unref(expr1);
				return NULL;
			}
		}
		switch(psi_formula->type) {
		case psiconv_formula_op_lt:
			return gnm_expr_new_binary (expr1,GNM_EXPR_OP_LT,expr2);
		case psiconv_formula_op_le:
			return gnm_expr_new_binary (expr1,GNM_EXPR_OP_LTE,expr2);
		case psiconv_formula_op_gt:
			return gnm_expr_new_binary (expr1,GNM_EXPR_OP_GT,expr2);
		case psiconv_formula_op_ge:
			return gnm_expr_new_binary (expr1,GNM_EXPR_OP_GTE,expr2);
		case psiconv_formula_op_ne:
			return gnm_expr_new_binary (expr1,GNM_EXPR_OP_NOT_EQUAL,expr2);
		case psiconv_formula_op_eq:
			return gnm_expr_new_binary (expr1,GNM_EXPR_OP_EQUAL,expr2);
		case psiconv_formula_op_add:
			return gnm_expr_new_binary (expr1,GNM_EXPR_OP_ADD,expr2);
		case psiconv_formula_op_sub:
			return gnm_expr_new_binary (expr1,GNM_EXPR_OP_SUB,expr2);
		case psiconv_formula_op_mul:
			return gnm_expr_new_binary (expr1,GNM_EXPR_OP_MULT,expr2);
		case psiconv_formula_op_div:
			return gnm_expr_new_binary (expr1,GNM_EXPR_OP_DIV,expr2);
		case psiconv_formula_op_pos:
			return gnm_expr_new_unary (GNM_EXPR_OP_UNARY_PLUS,expr1);
		case psiconv_formula_op_neg:
			return gnm_expr_new_unary (GNM_EXPR_OP_UNARY_NEG,expr1);
		default:
			gnm_expr_unref(expr1);
			gnm_expr_unref(expr2);
			return NULL;
		}
	} else if (kind == 3) {
		switch(psi_formula->type) {
		case psiconv_formula_dat_cellref: {
			CellRef cr;
			return gnm_expr_new_cellref (cellref_init (&cr,
				psi_formula->data.dat_cellref.row.offset,
			        psi_formula->data.dat_cellref.row.absolute,
			        psi_formula->data.dat_cellref.column.offset,
			        psi_formula->data.dat_cellref.column.absolute));
		}

		case psiconv_formula_op_bra:
			if (!(psi_form1 = psiconv_list_get
			                  (psi_formula->data.fun_operands,0)))
				return NULL;
			return parse_subexpr(psi_form1);
		default:
		}
	}

	return NULL;
}

static GnmExpr *
expr_new_from_formula (const psiconv_sheet_cell psi_cell,
		       const psiconv_formula_list psi_formulas)
{
	psiconv_formula formula;

	formula = psiconv_get_formula (psi_formulas, psi_cell->ref_formula);

	return (formula != NULL) ?  parse_subexpr (formula) : NULL;
}

static void
add_cell (Sheet *sheet, const psiconv_sheet_cell psi_cell,
	  const psiconv_formula_list psi_formulas, const MStyle * default_style)
{
	Cell *cell;
	Value *val;
	GnmExpr *expr;
	psiconv_formula psi_formula;

	cell = sheet_cell_fetch (sheet, psi_cell->column, psi_cell->row);
	if (!cell)
		return;

	val = value_new_from_psi_cell (psi_cell);

	if (psi_cell->calculated)
		expr = expr_new_from_formula (psi_cell, psi_formulas);

	if (expr != NULL) {
		/* TODO : is there a notion of parse format ?
		 * How does it store a user entered date ?
		 */
		if (val != NULL)
			cell_set_expr_and_value (cell, expr, val, NULL, TRUE);
		else
			cell_set_expr (cell, expr, NULL);
	} else if (val != NULL) {
		/* TODO : is there a notion of parse format ?
		 * How does it store a user entered date ?
		 */
		cell_set_value (cell, val, NULL);
	} else {
		/* TODO : send this warning to iocontext with details of
		 * which sheet and cell.
		 */
		g_warning ("Cell with no value or expression ?");
	}

	/* TODO: Perhaps this must be moved above set_format */
	set_style(sheet,psi_cell->row,psi_cell->column,psi_cell->layout,
	          default_style);
}

static void
add_cells(Sheet *sheet, const psiconv_sheet_cell_list psi_cells,
          const psiconv_formula_list psi_formulas,
          const MStyle *default_style)
{
	psiconv_u32 i;
	psiconv_sheet_cell psi_cell;

	/* FIXME : Without seeing the psiconv code this seems
	 * VERY inefficent.  index & len are O(n) operations on lists.
	 * Why not just walk the list ?
	 */
	for (i = 0; i < psiconv_list_length(psi_cells); i++) {
		/* If psiconv_list_get fails, something is very wrong... */
		if ((psi_cell = psiconv_list_get(psi_cells,i)))
			add_cell(sheet,psi_cell,psi_formulas, default_style);
	}
}

static void
add_worksheet(Workbook *wb, psiconv_sheet_worksheet psi_worksheet,int nr,
              psiconv_formula_list psi_formulas)
{
	Sheet *sheet;
	char *sheet_name;
	MStyle *default_style;

	sheet_name = g_strdup_printf (_("Sheet%d"),nr);
	sheet = sheet_new (wb, sheet_name);
	g_free (sheet_name);
	if (!sheet)
		return;

	/* Default layout */
	default_style = mstyle_new_default();
	if (!default_style) {
		sheet_destroy(sheet);
		return;
	}
	set_layout(default_style,psi_worksheet->default_layout);

	/* TODO: Add show_zeros */

	add_cells(sheet,psi_worksheet->cells,psi_formulas,default_style);

	/* TODO: What about the NULL? */
	sheet_flag_recompute_spans(sheet);
	workbook_sheet_attach (wb,sheet,NULL);
}

static void
add_workbook(Workbook *wb, psiconv_sheet_workbook_section psi_workbook)
{
	psiconv_u32 i;
	psiconv_sheet_worksheet psi_worksheet;

	/* TODO: Perhaps add formulas and share them? */
	for (i = 0; i < psiconv_list_length(psi_workbook->worksheets); i++) {
		/* If psiconv_list_get fails, something is very wrong... */
		if ((psi_worksheet = psiconv_list_get
		                                 (psi_workbook->worksheets,i)))
			add_worksheet(wb,psi_worksheet,i,
			              psi_workbook->formulas);
	}

	workbook_queue_all_recalc (wb);
}

static void
add_sheetfile(Workbook *wb, psiconv_sheet_f psi_file)
{
	/* TODO: Add page_sec */
	/* TODO: Add status_sec */
	add_workbook(wb,psi_file->workbook_sec);
}


static psiconv_buffer
psiconv_file_to_buffer(FILE *file)
{
	psiconv_buffer buf;

	if (!file) {
		return NULL;
	}
	if (!(buf = psiconv_buffer_new())) {
		return NULL;
	}
	if (psiconv_buffer_fread_all(buf,file)) {
		psiconv_buffer_free(buf);
		return NULL;
	}
	return buf;
}

void
psiconv_read (IOContext *io_context, Workbook *wb, FILE *file)
{
	psiconv_buffer buf;
	psiconv_file psi_file;

	if (!(buf = psiconv_file_to_buffer(file))) {
		gnumeric_io_error_info_set (io_context,
		                            error_info_new_str(_("Error while reading psiconv file.")));
		return;
	}

	/* For temporary debugging purposes */
	/* psiconv_verbosity = PSICONV_VERB_DEBUG; */

	if (psiconv_parse(buf,&psi_file)) {
		gnumeric_io_error_info_set (io_context,
		                            error_info_new_str(_("Error while parsing psiconv file.")));
		psiconv_buffer_free(buf);
		return;
	}

	psiconv_buffer_free(buf);

	add_sheetfile(wb,psi_file->file);

	psiconv_free_file(psi_file);
}

gboolean
psiconv_read_header (FILE *file)
{
	gboolean res;
	psiconv_buffer buf;

	if (!(buf = psiconv_file_to_buffer(file))) {
		return FALSE;
	}
	res =  psiconv_file_type(buf,NULL,NULL) == psiconv_sheet_file;
	psiconv_buffer_free(buf);
	return res;
}

