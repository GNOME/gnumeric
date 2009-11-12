/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * solver.c:  The Solver's core system.
 *
 * Author:
 *   Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *
 * (C) Copyright 1999, 2000, 2002 by Jukka-Pekka Iivonen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>

#include "parse-util.h"
#include "solver.h"
#include "func.h"
#include "cell.h"
#include "sheet.h"
#include "workbook.h"
#include "sheet-style.h"
#include "dependent.h"
#include "dialogs.h"
#include "mstyle.h"
#include "value.h"
#include "ranges.h"
#include "expr.h"
#include "clipboard.h"
#include "commands.h"
#include "mathfunc.h"
#include "analysis-tools.h"
#include "api.h"
#include "gutils.h"
#include <goffice/goffice.h>
#include "xml-sax.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

/* ------------------------------------------------------------------------- */

gchar *
solver_reports (WorkbookControl *wbc, Sheet *sheet, SolverResults *res,
		gboolean answer, gboolean sensitivity, gboolean limits,
		gboolean performance, gboolean program, gboolean dual)
{
	return NULL;
}

/* ------------------------------------------------------------------------- */

GnmCell *
solver_get_input_var (SolverResults *res, int n)
{
        return res->input_cells_array[n];
}

GnmSolverConstraint*
solver_get_constraint (SolverResults *res, int n)
{
        return res->constraints_array[n];
}

/* ------------------------------------------------------------------------- */
