/*
 * excelplugins.c:
 *
 * Adapter interface to load worksheet functions defined in Excel
 * plugins (also known as XLLs).  Note that this adapter interface
 * only works for XLL worksheet functions that expect all their
 * arguments to be of type OPER (type 'P' in the Excel SDK
 * documentation) or of type eXtended exceL OPER (type 'R' in the
 * Excel SDK documentation known as XLOPER) and that return their
 * results as an OPER (type 'P'). Note that type 'R' can give rise to
 * sheet references to be passed in which this code here cannot handle
 * whence it is best to only use type 'P'.
 *
 * Author:
 *   Peter Jaeckel (peter.jaeckel@gmail.com)
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <func.h>
#include <value.h>
#include <gnm-i18n.h>
#include <gnm-plugin.h>
#include <goffice/utils/go-glib-extras.h>
#include <stdint.h>
#include <glib/gstdio.h>
#include <workbook.h>
#include <sheet.h>
#include <cell.h>
#include <string.h>

#ifdef WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

#define g_slice_new_array0(T,c) ((T*) g_slice_alloc0 ((sizeof (T))*(c)))
#define ALLOC_ARRAY(T,c) ( g_slice_new_array0 (T,c) )
#define FREE_ARRAY(p,c)  ( g_slice_free1 (sizeof(*p)*(c),(p)) )

GNM_PLUGIN_MODULE_HEADER;

/***************************************************************************/

/* When Gnumeric calls a worksheet function, it directly calls into
 * the given entry point, and passes in data in Gnumeric style. Since
 * an external XLL's exposed functions expect data in Excel format,
 * those functions' entry points cannot simply be forwarded to
 * Gnumeric. Instead, a conversion function is invoked. Luckily, the
 * Gnumeric infrastructure provides EvalFunctionInfo pointer at the
 * time of call. Given the EvalFunctionInfo *ei, we have the name of
 * the function we are calling in ei->func_call->func->name. We can
 * use this, to look up which function is actually to be called. For
 * the look-up, we create a global function map (name->some structure)
 * at the time of go_plugin_init that we peruse later. At the heart of
 * the communication between Gnumeric and the XLL is a little adapter
 * function, sometimes called a "shim". This function is the one that
 * is actually called by Gnumeric when it tries to call one of the
 * functions that actually live in the XLL. The shim is called
 *
 *	genericXLLFunction () .
 *
 * Once it has looked up the function name in its information
 * database, it knows how many arguments of what type the external XLL
 * function expects, and can convert argument types, call the actual
 * XLL function, convert the returned value, clean up memory, and
 * return to Gnumeric.
 *
 */

#if defined( WIN32 ) || defined( WIN64 )
#include <windef.h>
#else
#include "win32replacements.h"
#endif

#include "xlcall.h"

/* All bits that are mutually exclusive in the type field of an xloper. */
#define xltypeType       0x0FFF

struct _XLL {
	gchar * name;
	GModule * handle;
	void (*xlAutoFree)(XLOPER*);
	unsigned long number_of_functions;
};

typedef struct _XLL XLL;

struct _XLLFunctionInfo {
	XLL* xll; /* Not owned. Included for availability of information during callbacks, and for access to xlAutoFree. */
	XLOPER* (*xll_function)(void);
	gchar* category;
	GnmFuncDescriptor gnm_func_descriptor;
	guint number_of_arguments;
	GnmFunc* gnm_func;
};

typedef struct _XLLFunctionInfo XLLFunctionInfo;

static GModule *xlcall32_handle = NULL;
static void (*register_actual_excel4v)(void*p) = NULL;

static GSList* XLLs = NULL;

/* This balanced tree maps from function name (gchar*) to
   XLLFunctionInfo*. It is consulted when gnumeric attempts to call
   one of the registered functions. Note that the memory of any key is
   handled as part of the memory of its associated value since the key
   is the same as the element gnm_func_descriptor.name. */
static GTree* xll_function_info_map = NULL;

static GnmFuncEvalInfo * current_eval_info = NULL;
static XLL * currently_called_xll = NULL;

/* The limit of 30 arguments is hardwired in Excel. We take advantage of this below when we convert from argv[] calling
   convention to vararg calling convention (...) */
enum { MAXIMUM_NUMBER_OF_EXCEL_FUNCTION_ARGUMENTS = 30 };

/* If data types other than XLOPER* are to be allowed, then something
   like the Foreign Function Interface library (libffi) would have to
   be used in order to get the arguments pushed on the stack prior to
   calling the XLL function. As long as all data are XLOPER*, we can
   use this one function type to call all XLL functions. */
typedef XLOPER*(*XLLFunctionWithVarArgs)(XLOPER* first, ...);

#define CASE(x) case x: return #x

#ifdef THIS_IS_HERE_FOR_DEBUG_PURPOSES
static const char *
gnm_value_type_name (const GnmValue*g)
{
	if (NULL != g) {
		switch (g->v_any.type) {
		CASE(VALUE_EMPTY);
		CASE(VALUE_BOOLEAN);
		CASE(VALUE_FLOAT);
		CASE(VALUE_ERROR);
		CASE(VALUE_STRING);
		CASE(VALUE_CELLRANGE);
		CASE(VALUE_ARRAY);
		default: return "<unknown>";
		}
	}
	return "(nil)";
}
#endif

static const char *
xloper_type_name (const XLOPER*x)
{
	if (NULL != x) {
		switch (x->xltype & xltypeType) {
		CASE(xltypeNum);
		CASE(xltypeStr);
		CASE(xltypeBool);
		CASE(xltypeRef);
		CASE(xltypeErr);
		CASE(xltypeFlow);
		CASE(xltypeMulti);
		CASE(xltypeMissing);
		CASE(xltypeNil);
		CASE(xltypeSRef);
		CASE(xltypeInt);
		default: return "<unknown>";
		}
	}
	return "(nil)";
}

static void
unsupported_xloper_type (const XLOPER*x)
{
	g_warning ("Unsupported xloper type \"%s\"",
		   xloper_type_name (x));
}

static GnmStdError
gnm_value_error_from_xloper (const XLOPER *x)
{
	g_return_val_if_fail (NULL != x, GNM_ERROR_UNKNOWN);
	g_return_val_if_fail ((x->xltype & xltypeType)==xltypeErr,
			      GNM_ERROR_UNKNOWN);

	switch (x->val.err) {
	case xlerrNull:  return GNM_ERROR_NULL;
	case xlerrDiv0:  return GNM_ERROR_DIV0;
	case xlerrValue: return GNM_ERROR_VALUE;
	case xlerrRef:   return GNM_ERROR_REF;
	case xlerrName:  return GNM_ERROR_NAME;
	case xlerrNum:   return GNM_ERROR_NUM;
	case xlerrNA:    return GNM_ERROR_NA;
	default:         return GNM_ERROR_UNKNOWN;
	}
}

static WORD
xloper_error_code_from_gnm_value (const GnmValue* g)
{
	switch (value_error_classify (g)) {
	case GNM_ERROR_NULL:  return xlerrNull;
	case GNM_ERROR_DIV0:  return xlerrDiv0;
	case GNM_ERROR_VALUE: return xlerrValue;
	case GNM_ERROR_REF:   return xlerrRef;
	case GNM_ERROR_NAME:  return xlerrName;
	case GNM_ERROR_NUM:   return xlerrNum;
	case GNM_ERROR_NA:    return xlerrNA;
	default:              return xlerrValue;
	}
}

/*
 *     For most standard types, there is a natural mapping between XLOPER and GnmValue.
 *     In addition, we use the following correspondence:
 *
 *                              |   GnmValue * v               |    XLOPER * x                                              |
 *      ------------------------+------------------------------+------------------------------------------------------------+
 *                              |   v       == NULL           <->   x->xltype == xltypeMissing                              |
 *                              |   v->type == VALUE_EMPTY    <->   x->xltype == xltypeNil                                  |
 *                              |   v->type == VALUE_ERROR    <->   x->xltype == xltypeErr                                  |
 *      ------------------------+------------------------------+------------------------------------------------------------+
 *
 */

static char *
pascal_string_from_c_string (const char *s)
{
	char *o = NULL;
	if (NULL != s) {
		guint l = strlen (s);
		g_return_val_if_fail (l<(UINT_MAX-2U),NULL);
		o = g_malloc (l+2U);
		g_strlcpy (o+1,s,l+1);
		if (l>UCHAR_MAX)
			l=UCHAR_MAX;
		o[0] = (unsigned char)l;
	}
	return o;
}

static char *
c_string_from_pascal_string (const char *s)
{
	if (NULL != s) {
		const guint m = ((unsigned char)s[0])+1U;
		char * o = g_malloc (m);
		g_strlcpy (o,s+1,m);
		return o;
	}
	return NULL;
}

static void
copy_construct_xloper_from_gnm_value (XLOPER*out, const GnmValue*in,
				      GnmFuncEvalInfo *ei)
{
	g_return_if_fail (NULL != out);

	out->xltype = xltypeMissing;
	out->val.num = 0;

	if (NULL != in) {
		switch (in->v_any.type) {
		case VALUE_EMPTY:
			out->xltype = xltypeNil;
			break;
		case VALUE_BOOLEAN:
			out->xltype = xltypeBool;
			out->val.boolean = (WORD)value_get_as_checked_bool (in);
			break;
		case VALUE_FLOAT:
			out->xltype = xltypeNum;
			out->val.num = in->v_float.val;
			break;
		case VALUE_ERROR:
			out->xltype = xltypeErr;
			out->val.err = xloper_error_code_from_gnm_value (in);
			break;
		case VALUE_STRING:
			out->xltype = xltypeStr;
			out->val.str = pascal_string_from_c_string (value_peek_string (in));
			break;
		case VALUE_CELLRANGE: {
			GnmSheetRange sr;
			GnmRangeRef const *rr = value_get_rangeref (in);
			Sheet *end_sheet = NULL;
			GnmValue *cell_value;
			GnmCell  *cell;
			gnm_rangeref_normalize (rr, ei->pos, &sr.sheet, &end_sheet, &sr.range);
			if (sr.sheet != end_sheet) {
				/* We don't attempt to flatten a 3D range to an array. */
				g_warning (_("Cannot convert 3D cell range to XLOPER."));
			} else {
				int m = sr.range.end.col-sr.range.start.col+1;
				int n = sr.range.end.row-sr.range.start.row+1;
				int i, j;

				out->xltype = xltypeMulti;
				out->val.array.lparray = ALLOC_ARRAY (XLOPER,m*n);
				out->val.array.columns = m;
				out->val.array.rows = n;
				for (i = 0; i < m; ++i) {
					for (j = 0;j<n; ++j) {
						cell = sheet_cell_get (sr.sheet,sr.range.start.col+i,sr.range.start.row+j);
						cell_value = NULL;
						if (NULL != cell) {
							gnm_cell_eval (cell);
							cell_value=cell->value;
						}
						copy_construct_xloper_from_gnm_value (out->val.array.lparray+i+j*m,cell_value,ei);
					}
				}
			}
			break;
		}
		case VALUE_ARRAY: {
			int m = in->v_array.x;
			int n = in->v_array.y;
			int i, j;

			out->xltype = xltypeMulti;
			out->val.array.lparray = ALLOC_ARRAY (XLOPER,m*n);
			out->val.array.columns = m;
			out->val.array.rows = n;
			for (i = 0; i < m; ++i) {
				for (j = 0;j < n; ++j) {
					copy_construct_xloper_from_gnm_value (out->val.array.lparray+i+j*m,in->v_array.vals[i][j],ei);
				}
			}
			break;
		}
		default:;
			g_warning (_("Unsupported GnmValue type (%d)"),in->v_any.type);
		}
	}
}

static void
delete_string (gchar **s)
{
	if (s) {
		g_free (*s);
		*s = NULL;
	}
}

static void
destruct_xloper (XLOPER*x)
{
	if (NULL != x) {
		switch (x->xltype & xltypeType) {
		case xltypeNum:
			break;
		case xltypeStr:
			delete_string (&x->val.str);
			break;
		case xltypeBool:
			break;
		case xltypeRef:
			if (NULL != x->val.mref.lpmref &&
			    x->val.mref.lpmref->count != 1) {
				unsupported_xloper_type (x);
			} else {
				if (NULL != x->val.mref.lpmref)
					FREE_ARRAY (x->val.mref.lpmref, 1);
				x->val.mref.lpmref = NULL;
			}
			break;
		case xltypeErr:
			break;
		case xltypeFlow:
			unsupported_xloper_type (x);
			break;
		case xltypeMulti: {
			int n = x->val.array.rows*x->val.array.columns;
			int i;
			for (i = 0; i < n; ++i) {
				destruct_xloper (x->val.array.lparray+i);
			}
			FREE_ARRAY (x->val.array.lparray,n);
			break;
		}
		case xltypeMissing:
			break;
		case xltypeNil:
			break;
		case xltypeSRef:
			unsupported_xloper_type (x);
			break;
		case xltypeInt:
			break;
		default:
			unsupported_xloper_type (x);
		}
		x->xltype = xltypeNil;
	}
}

static GnmValue *
new_gnm_value_from_xloper (const XLOPER*x)
{
	GnmValue * g = NULL;
	if (NULL != x) {
		switch (x->xltype & xltypeType) {
		case xltypeNum:
			g = value_new_float (x->val.num);
			break;
		case xltypeStr: {
			char *o = NULL;
			const char *s = x->val.str;
			if (NULL != s) {
				guint m = ((unsigned char)s[0]) + 1U;
				o = g_new (char, m);
				g_strlcpy (o, s + 1, m);
			}
			g = value_new_string_nocopy (o);
			break;
		}
		case xltypeBool:
			g = value_new_bool (x->val.boolean);
			break;
		case xltypeRef:
			unsupported_xloper_type (x);
			break;
		case xltypeErr:
			g = value_new_error_std (NULL, gnm_value_error_from_xloper (x));
			break;
		case xltypeFlow:
			unsupported_xloper_type (x);
			break;
		case xltypeMulti: {
			guint m = x->val.array.columns;
			guint n = x->val.array.rows;
			if (m > 0 && n > 0) {
				guint i;
				g = value_new_array_empty (m,n);
				for (i = 0; i < m; ++i) {
					guint j;
					for (j = 0; j < n; ++j) {
						g->v_array.vals[i][j] =
							new_gnm_value_from_xloper (x->val.array.lparray + i + j * m);
					}
				}
			} else {
				g = value_new_error_std (NULL, GNM_ERROR_VALUE);
			}
			break;
		}
		case xltypeMissing:
			break;
		case xltypeNil:
			g = value_new_empty ();
			break;
		case xltypeSRef:
			unsupported_xloper_type (x);
			break;
		case xltypeInt:
			g = value_new_int (x->val.w);
			break;
		default:
			unsupported_xloper_type (x);
		}
	} else {
		g = value_new_error_std (NULL, GNM_ERROR_NUM);
	}
	return g;
}

static GnmValue *
genericXLLFunction (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	XLOPER x[MAXIMUM_NUMBER_OF_EXCEL_FUNCTION_ARGUMENTS], *r = NULL;
	XLLFunctionWithVarArgs func = NULL;
	GnmValue *g = NULL;
	guint i,m;
	const XLLFunctionInfo*info = NULL;
	GnmFunc const *gfunc = gnm_eval_info_get_func (ei);

	g_assert (NULL != xll_function_info_map);
	info=g_tree_lookup (xll_function_info_map, gfunc->name);
	g_assert (NULL != info);
	m = gnm_eval_info_get_arg_count (ei);
	m = MAX (m, MAXIMUM_NUMBER_OF_EXCEL_FUNCTION_ARGUMENTS);
	for (i = 0; i < m; ++i)
		copy_construct_xloper_from_gnm_value (x+i,argv[i],ei);
#if 0
	m = info->number_of_arguments;
	m = MAX (m, MAXIMUM_NUMBER_OF_EXCEL_FUNCTION_ARGUMENTS);
#else
	// MW 20180515: it's unclear to me what is needed, but the above
	// cannot be right.  This should be safe.
	m = MAXIMUM_NUMBER_OF_EXCEL_FUNCTION_ARGUMENTS;
#endif
	for (; i < m; ++i)
		x[i].xltype=xltypeMissing;
	func = (XLLFunctionWithVarArgs)info->xll_function;
	g_assert (NULL != func);
	currently_called_xll = info->xll;
	current_eval_info    = ei;
	switch (info->number_of_arguments) {
	default: g_assert_not_reached ();
	case  0: r=info->xll_function (); break;
	/*
	  bash script to generate code below
	  n=0; while [ $n -le 30 ] ; do echo -n "	case "; if [ $n -lt 10 ] ; then echo -n " "; fi; echo -n "$n: r=func ("; j = 0; while [ $j -lt $n ]; do echo -n "x+$j"; if [ $[ $j + 1] -lt $n ] ; then echo -n "," ; fi ; j=$[ $j + 1 ] ; done ; echo "); break;" ; n=$[ $n + 1 ] ; done
	*/
	case  1: r=func (x+0); break;
	case  2: r=func (x+0,x+1); break;
	case  3: r=func (x+0,x+1,x+2); break;
	case  4: r=func (x+0,x+1,x+2,x+3); break;
	case  5: r=func (x+0,x+1,x+2,x+3,x+4); break;
	case  6: r=func (x+0,x+1,x+2,x+3,x+4,x+5); break;
	case  7: r=func (x+0,x+1,x+2,x+3,x+4,x+5,x+6); break;
	case  8: r=func (x+0,x+1,x+2,x+3,x+4,x+5,x+6,x+7); break;
	case  9: r=func (x+0,x+1,x+2,x+3,x+4,x+5,x+6,x+7,x+8); break;
	case 10: r=func (x+0,x+1,x+2,x+3,x+4,x+5,x+6,x+7,x+8,x+9); break;
	case 11: r=func (x+0,x+1,x+2,x+3,x+4,x+5,x+6,x+7,x+8,x+9,x+10); break;
	case 12: r=func (x+0,x+1,x+2,x+3,x+4,x+5,x+6,x+7,x+8,x+9,x+10,x+11); break;
	case 13: r=func (x+0,x+1,x+2,x+3,x+4,x+5,x+6,x+7,x+8,x+9,x+10,x+11,x+12); break;
	case 14: r=func (x+0,x+1,x+2,x+3,x+4,x+5,x+6,x+7,x+8,x+9,x+10,x+11,x+12,x+13); break;
	case 15: r=func (x+0,x+1,x+2,x+3,x+4,x+5,x+6,x+7,x+8,x+9,x+10,x+11,x+12,x+13,x+14); break;
	case 16: r=func (x+0,x+1,x+2,x+3,x+4,x+5,x+6,x+7,x+8,x+9,x+10,x+11,x+12,x+13,x+14,x+15); break;
	case 17: r=func (x+0,x+1,x+2,x+3,x+4,x+5,x+6,x+7,x+8,x+9,x+10,x+11,x+12,x+13,x+14,x+15,x+16); break;
	case 18: r=func (x+0,x+1,x+2,x+3,x+4,x+5,x+6,x+7,x+8,x+9,x+10,x+11,x+12,x+13,x+14,x+15,x+16,x+17); break;
	case 19: r=func (x+0,x+1,x+2,x+3,x+4,x+5,x+6,x+7,x+8,x+9,x+10,x+11,x+12,x+13,x+14,x+15,x+16,x+17,x+18); break;
	case 20: r=func (x+0,x+1,x+2,x+3,x+4,x+5,x+6,x+7,x+8,x+9,x+10,x+11,x+12,x+13,x+14,x+15,x+16,x+17,x+18,x+19); break;
	case 21: r=func (x+0,x+1,x+2,x+3,x+4,x+5,x+6,x+7,x+8,x+9,x+10,x+11,x+12,x+13,x+14,x+15,x+16,x+17,x+18,x+19,x+20); break;
	case 22: r=func (x+0,x+1,x+2,x+3,x+4,x+5,x+6,x+7,x+8,x+9,x+10,x+11,x+12,x+13,x+14,x+15,x+16,x+17,x+18,x+19,x+20,x+21); break;
	case 23: r=func (x+0,x+1,x+2,x+3,x+4,x+5,x+6,x+7,x+8,x+9,x+10,x+11,x+12,x+13,x+14,x+15,x+16,x+17,x+18,x+19,x+20,x+21,x+22); break;
	case 24: r=func (x+0,x+1,x+2,x+3,x+4,x+5,x+6,x+7,x+8,x+9,x+10,x+11,x+12,x+13,x+14,x+15,x+16,x+17,x+18,x+19,x+20,x+21,x+22,x+23); break;
	case 25: r=func (x+0,x+1,x+2,x+3,x+4,x+5,x+6,x+7,x+8,x+9,x+10,x+11,x+12,x+13,x+14,x+15,x+16,x+17,x+18,x+19,x+20,x+21,x+22,x+23,x+24); break;
	case 26: r=func (x+0,x+1,x+2,x+3,x+4,x+5,x+6,x+7,x+8,x+9,x+10,x+11,x+12,x+13,x+14,x+15,x+16,x+17,x+18,x+19,x+20,x+21,x+22,x+23,x+24,x+25); break;
	case 27: r=func (x+0,x+1,x+2,x+3,x+4,x+5,x+6,x+7,x+8,x+9,x+10,x+11,x+12,x+13,x+14,x+15,x+16,x+17,x+18,x+19,x+20,x+21,x+22,x+23,x+24,x+25,x+26); break;
	case 28: r=func (x+0,x+1,x+2,x+3,x+4,x+5,x+6,x+7,x+8,x+9,x+10,x+11,x+12,x+13,x+14,x+15,x+16,x+17,x+18,x+19,x+20,x+21,x+22,x+23,x+24,x+25,x+26,x+27); break;
	case 29: r=func (x+0,x+1,x+2,x+3,x+4,x+5,x+6,x+7,x+8,x+9,x+10,x+11,x+12,x+13,x+14,x+15,x+16,x+17,x+18,x+19,x+20,x+21,x+22,x+23,x+24,x+25,x+26,x+27,x+28); break;
	case 30: r=func (x+0,x+1,x+2,x+3,x+4,x+5,x+6,x+7,x+8,x+9,x+10,x+11,x+12,x+13,x+14,x+15,x+16,x+17,x+18,x+19,x+20,x+21,x+22,x+23,x+24,x+25,x+26,x+27,x+28,x+29); break;
	}
	g=new_gnm_value_from_xloper (r);
	if ( r && (r->xltype & xlbitDLLFree) && (NULL != info->xll->xlAutoFree) )
		info->xll->xlAutoFree ( r );
	currently_called_xll = NULL;
	current_eval_info    = NULL;
	for (i = 0; i < info->number_of_arguments; ++i)
		destruct_xloper (x+i);
	return g;
}

/***************************************************************************/

static int
gnm_func_help_entries (int number_of_arguments)
{
	 /* NAME,DESCRIPTION,EXCEL,ARG1,...ARGn,END */
	return number_of_arguments + 4;
}

static void
free_xll_function_info (gpointer data)
{
	XLLFunctionInfo *info= (XLLFunctionInfo *)data;
	const guint n = info->number_of_arguments;
	if (NULL != info->gnm_func) {
		g_object_unref (info->gnm_func);
		info->gnm_func = NULL;
	}
	delete_string (&info->category);
	delete_string ((gchar**)&info->gnm_func_descriptor.name);
	delete_string ((gchar**)&info->gnm_func_descriptor.arg_spec);
	if (NULL != info->gnm_func_descriptor.help) {
		guint i, m=gnm_func_help_entries (n);
		for (i = 0; i < m; ++i) {
			delete_string ((gchar**)&info->gnm_func_descriptor.help[i].text);
		}
		FREE_ARRAY ((GnmFuncHelp*)info->gnm_func_descriptor.help,m);
		info->gnm_func_descriptor.help = NULL;
	}
	info->gnm_func_descriptor.fn_args = NULL;
	info->number_of_arguments = 0;
	info->xll_function = NULL;
	info->gnm_func_descriptor.fn_args = NULL;
	FREE_ARRAY (info,1);
}

typedef int WINAPI (*XLAutoOpenFunc)(void);
typedef int WINAPI (*XLAutoCloseFunc)(void);

static void
free_XLL (gpointer data)
{
	XLL*xll=(XLL*)data;
	if (NULL != xll->handle) {
		XLAutoCloseFunc xlAutoCloseFunc = NULL;
		g_module_symbol (xll->handle, "xlAutoClose", (gpointer) &xlAutoCloseFunc);
		if (NULL != xlAutoCloseFunc) {
			currently_called_xll = xll;
			xlAutoCloseFunc ();
			currently_called_xll = NULL;
		}
		if (!g_module_close (xll->handle))
			g_warning (_("%s: %s"), xll->name, g_module_error ());
		xll->handle = NULL;
	}
	delete_string (&xll->name);
	FREE_ARRAY (xll,1);
}

static gint
g_strcmp0_with_ignored_data (gconstpointer str1, gconstpointer str2,
			     gpointer user_data)
{
	return g_strcmp0 (str1, str2);
}

static gboolean
add_xll_function (const char *exported_function_symbol, XLLFunctionInfo *info)
{
	g_module_symbol (info->xll->handle, exported_function_symbol, (gpointer) &info->xll_function);
	if (NULL != info->xll_function) {
		XLLFunctionInfo* info_in_map = NULL;
		GnmFunc *gnm_func = NULL;
		if (NULL == xll_function_info_map)
			xll_function_info_map = g_tree_new_full (g_strcmp0_with_ignored_data,NULL,NULL,free_xll_function_info);
		info_in_map = g_tree_lookup (xll_function_info_map,info->gnm_func_descriptor.name);
		if (NULL != info_in_map)
			g_warning (_("Overriding function %s from XLL/DLL/SO file %s with function of the same name from XLL/DLL/SO file %s."),
				  info->gnm_func_descriptor.name,info_in_map->xll->name,info->xll->name);
		gnm_func = gnm_func_add (gnm_func_group_fetch (info->category,NULL),&info->gnm_func_descriptor,NULL);
		if (gnm_func) {
			info->gnm_func=gnm_func;
			g_tree_insert (xll_function_info_map,(gpointer)info->gnm_func_descriptor.name,info);
			++info->xll->number_of_functions;
			return TRUE;
		}
	} else {
		g_warning (_("Failed to find function \"%s\" in XLL/DLL/SO %s .\n"),info->gnm_func_descriptor.name,info->xll->name);
	}
	return FALSE;
}

static int
actual_Excel4v (int xlfn, XLOPER* operRes, int count, XLOPER** opers)
{
	switch (xlfn) {
	case xlfRegister: {
		XLLFunctionInfo* info = NULL;
		GnmFuncHelp *    help = NULL;
		GnmFuncFlags    flags = GNM_FUNC_SIMPLE; /* We may have to include GNM_FUNC_RETURNS_NON_SCALAR here
							    since all Excel functions may return an array. Having said
							    that, all array functions seem to work fine without this
							    flag. */
		GString * argument_specifications = g_string_sized_new (21);
		gchar ** arg_names = NULL, *exported_function_symbol = NULL, *function_description = NULL;
		guint number_of_arguments = 0;
		gboolean success = FALSE;
		int i,j,m,n;
		/*
		 * Check http://msdn.microsoft.com/en-us/library/bb687900.aspx for details.
		 *
		 * opers[ 0] : DLL name (string). Might have been queried by the XLL by calling in here as xlGetName prior to registering (that's how it's usually done).
		 *             This feature allows, in principle, for one XLL (the one calling Excel4v) to register functions that physically reside in another DLL.
			       We do not allow for this feature here.
			       IGNORED HERE.
		 * opers[ 1] : exported function symbol in XLL/DLL (string). To be located with g_module_symbol ().
			       MANDATORY.
		 * opers[ 2] : return and argument types string. Should be a string of only 'P's or 'R's in this context (apart from volatile markers etc).
			       MANDATORY.
		 * opers[ 3] : function name that is to be shown as the effective, user-visible, work sheet function.
			       DEFAULTS to opers[1].
		 * opers[ 4] : string of comma separated argument names.
			       DEFAULTS to NULL.
		 * opers[ 5] : function type as xltypeNum. 0.=hidden, 1.=function, 2.=command.
			       IGNORED HERE.
		 * opers[ 6] : function category as string.
			       DEFAULTS to "XLL functions".
		 * opers[ 7] : short cut text as string.
			       IGNORED HERE.
		 * opers[ 8] : help topic as string.
			       IGNORED HERE.
		 * opers[ 9] : function description as string.
			       DEFAULTS to NULL.
		 * opers[10 .. 10+n-1] : Help on the n arguments of actual function that is being registered.
			       DEFAULT to NULL.
		 */
		if (count<3) {
			g_warning (_("Excel plugin loader / xlfRegister: at least three XLOPER arguments must be provided (DLL name[ignored],exported name[mandatory],types string[mandatory]). You supplied %d in some function loaded from XLL/DLL/SO file %s."),count,currently_called_xll->name);
			return xlretInvCount; /* "An invalid number of arguments was entered. In versions up to Excel
						  2003, the maximum number of arguments any function can take is 30. In
						  Excel 2007, the maximum number is 255. Some require a fixed or minimum
						  number of arguments." */
		}
		if ( xltypeStr != (opers[1]->xltype & xltypeType) ||  xltypeStr != (opers[2]->xltype & xltypeType)) {
			g_warning (_("Excel plugin loader / xlfRegister: the second and third argument must be strings (DLL name[ignored],exported name[mandatory],types string[mandatory])."));
			return xlretInvXloper; /* "An invalid XLOPER or XLOPER12 was passed to the function, or an argument of the wrong type was used." */
		}
		m=0;
		if (opers[2]->val.str)
			m = (unsigned char)opers[2]->val.str[0];
		for (i = 0; i < m; ++i) {
			switch (opers[2]->val.str[i+1]) {
			case 'p':
			case 'P':
			case 'r':
			case 'R':
				g_string_append_c (argument_specifications,'?');
				++number_of_arguments;
				break;
			case '\r': /* Various junk we may as well ignore. */
			case '\n':
			case '\t':
			case ',':
			case ';':
			case ' ':
			case '#': /* This signals a request for this function or macro to have access to macro specific call back functions. Ignored.  */
				break;
			case '!':
				flags |= GNM_FUNC_VOLATILE;
				break;
			default:;
			}
		}
		exported_function_symbol=c_string_from_pascal_string (opers[1]->val.str);
		g_assert (argument_specifications->len==number_of_arguments);
		if (number_of_arguments>0) {
			--number_of_arguments; /* Subtract the return type count. The if statement is only to protect against sloppy XLLs. */
			argument_specifications->str[0] = '|'; /* All arguments are optional to an Excel function that accepts arguments type 'P'. */
		}
		info=ALLOC_ARRAY (XLLFunctionInfo,1);
		info->xll=currently_called_xll;
		info->number_of_arguments = number_of_arguments;
		info->gnm_func_descriptor.arg_spec=g_strdup (argument_specifications->str);
		info->gnm_func_descriptor.flags = flags;
		if (count>3 && (opers[3]->xltype & xltypeType)==xltypeStr) {
			info->gnm_func_descriptor.name = c_string_from_pascal_string (opers[3]->val.str);
		} else {
			info->gnm_func_descriptor.name = g_strdup (exported_function_symbol);
		}
		if (count>4 && (opers[4]->xltype & xltypeType)==xltypeStr) {
			gchar* xll_arg_names = c_string_from_pascal_string (opers[4]->val.str);
			arg_names = g_strsplit (xll_arg_names,",",number_of_arguments);
			delete_string (&xll_arg_names);
		}
		if (count>6 && (opers[6]->xltype & xltypeType)==xltypeStr) {
			info->category = c_string_from_pascal_string (opers[6]->val.str);
		} else {
			info->category = g_strdup ("XLL functions");
		}
		if (count>9 && (opers[9]->xltype & xltypeType)==xltypeStr) {
			function_description = c_string_from_pascal_string (opers[9]->val.str);
		}
		m=0;
		n=gnm_func_help_entries (number_of_arguments);
		help=ALLOC_ARRAY (GnmFuncHelp,n);
		g_assert (m<n);
		help[m].type=GNM_FUNC_HELP_NAME;
		help[m].text=g_strdup (info->gnm_func_descriptor.name);
		++m;
		g_assert (m<n);
		help[m].type=GNM_FUNC_HELP_DESCRIPTION;
		help[m].text=function_description;           /* Memory ownership handed over. */
		++m;
		g_assert (m<n);
		help[m].type=GNM_FUNC_HELP_EXCEL;
		help[m].text=g_strdup ("This function has been loaded from an Excel-compatible plugin (XLL). It is NOT a built-in function of Excel or Gnumeric.\n");
		/* We limit the number of argument strings we copy to the minimum of the number of arguments indicated
		   in the types string and the number of arguments strings given. We always provide enough space for as
		   many as indicated in the types string. */
		for (i = 10, j = 0;
		     i < count && i - 10 < (int)number_of_arguments;
		     ++i) {
			++m;
			help[m].type=GNM_FUNC_HELP_ARG;
			if ((opers[i]->xltype & xltypeType) == xltypeStr) {
				gchar * arg_name = (NULL != arg_names && NULL != arg_names[j]) ? arg_names[j++] : NULL;
				gchar * arg_help = c_string_from_pascal_string (opers[i]->val.str);
				g_assert (m < n);
				if (NULL != arg_name) {
					gchar *tmp=arg_help;
					arg_help = g_strconcat (arg_name,":",arg_help,NULL);
					delete_string (&tmp);
				}
				help[m].text = arg_help;
			}
		}
		++m;
		g_assert (m < n);
		help[m].type=GNM_FUNC_HELP_END;
		if (NULL != arg_names) {
			g_strfreev (arg_names);
			arg_names = NULL;
		}
		info->gnm_func_descriptor.help = help;
		info->gnm_func_descriptor.fn_args = genericXLLFunction;
		info->gnm_func_descriptor.impl_status = GNM_FUNC_IMPL_STATUS_COMPLETE;
		info->gnm_func_descriptor.test_status = GNM_FUNC_TEST_STATUS_BASIC;

		success = add_xll_function (exported_function_symbol,info);

		delete_string (&exported_function_symbol);

		if (success) {
			if (NULL != operRes) {
				operRes->xltype=xltypeNum;
				operRes->val.num=(unsigned long)info; /* This should be set to the resulting registration id. We use the info pointer as a proxy here. */
			}
			return xlretSuccess;
		}
		free_xll_function_info (info);
		return xlretInvXloper; /* "An invalid XLOPER or XLOPER12 was passed to the function, or an argument of the wrong type was used." */
	}
	case xlFree:
		while (count--) {
			destruct_xloper (opers[count]);
		}
		return xlretSuccess;
	case xlGetName:  /* The name of the DLL that is calling */
		if (NULL != operRes) {
			operRes->xltype=xltypeStr;
			operRes->val.str=pascal_string_from_c_string (currently_called_xll->name);
			return xlretSuccess;
		}
		return xlretInvXloper;
	case xlfRow:     /* Pass in the output of xlfCaller to get the row of the calling worksheet cell as an XLOPER of
			    type integer or number. Note that the output may be an array similar to the output from
			    xlSheetNm. */
		if (NULL != operRes) {
			if ( count>0 && xltypeRef==(opers[0]->xltype & xltypeType) && NULL != operRes->val.mref.lpmref) {
				operRes->xltype = xltypeInt;
				operRes->val.w = operRes->val.mref.lpmref->reftbl[1].rwFirst;
				return xlretSuccess;
			}
			if (NULL != current_eval_info) {
				operRes->xltype = xltypeInt;
				operRes->val.w = (short int) current_eval_info->pos->eval.row;
				return xlretSuccess;
			}
		}
		return xlretInvXloper;
	case xlfColumn:  /* Pass in the output of xlfCaller to get the column of the calling worksheet cell as an XLOPER of
			    type integer or number. Note that the output may be an array similar to the output from
			    xlSheetNm. */
		if (NULL != operRes) {
			if ( count>0 && xltypeRef==(opers[0]->xltype & xltypeType) && NULL != operRes->val.mref.lpmref) {
				operRes->xltype = xltypeInt;
				operRes->val.w = operRes->val.mref.lpmref->reftbl[1].colFirst;
				return xlretSuccess;
			}
			if (NULL != current_eval_info) {
				operRes->xltype = xltypeInt;
				operRes->val.w = (short int) current_eval_info->pos->eval.col;
				return xlretSuccess;
			}
		}
		return xlretInvXloper;


	/* Commonly called functions. None of these are fully implemented. */
	case xlfCaller:  /* Information about the location that is calling a worksheet function. The result of this can
			    be scalar XLOPER or a range, depending on whether a function is called as an array function,
			    or as a single cell function call. The result should be passed back in when calling
			    xlSheetNm, xlfRow, or xlfColumn. */
		if (NULL != operRes && NULL != current_eval_info) {
			operRes->xltype           = xltypeRef;
			operRes->val.mref.idSheet = current_eval_info->pos->sheet->index_in_wb; /* This is not globally unique but better than nothing. */
			operRes->val.mref.lpmref  = ALLOC_ARRAY (XLMREF,1);
			operRes->val.mref.lpmref->count=1;
			operRes->val.mref.lpmref->reftbl[1].rwFirst  = current_eval_info->pos->eval.row;
			operRes->val.mref.lpmref->reftbl[1].rwLast   = current_eval_info->pos->eval.row;
			operRes->val.mref.lpmref->reftbl[1].colFirst = current_eval_info->pos->eval.col;
			operRes->val.mref.lpmref->reftbl[1].colLast  = current_eval_info->pos->eval.col;
			return xlretSuccess;
		}
		return xlretInvXloper;
	case xlSheetNm:
		/* Pass in the output of xlfCaller to get the name of
		 * the calling worksheet as an XLOPER of type
		 * string. Note that the output of this can be an
		 * array if the output of xlfCaller was an array. To
		 * play safe, always check if it is an array, and use
		 * element (0,0) if it is, else use the scalar. */
		if (NULL != operRes && NULL != current_eval_info) {
			Sheet *sheet = current_eval_info->pos->sheet;
			int index_in_wb = sheet->index_in_wb;
			int row = current_eval_info->pos->eval.row;
			int col = current_eval_info->pos->eval.col;
			Workbook *workbook = sheet->workbook;
			if (count > 0 &&
			    xltypeRef == (opers[0]->xltype & xltypeType)) {
				index_in_wb = opers[0]->val.mref.idSheet;
				if (NULL != operRes->val.mref.lpmref) {
					row = operRes->val.mref.lpmref->reftbl[1].rwFirst;
					col = operRes->val.mref.lpmref->reftbl[1].colFirst;
				}
				sheet=workbook_sheet_by_index (workbook, index_in_wb);
			}
			operRes->xltype = xltypeStr;
			operRes->val.str = pascal_string_from_c_string (sheet->name_unquoted);
			return xlretSuccess;
		}
		return xlretInvXloper;
	case xlGetHwnd:  /* The WIN32 window handle of the "Excel" window */
	case xlAbort:    /* Query if the user hammered escape. May take one argument if the calling XLL wants to tell us that we are to continue.*/
		if (NULL != operRes) {
			operRes->xltype=xltypeBool;
			operRes->val.boolean=0;
		}
		return xlretSuccess;
	case xlcMessage: /* Set message bar. Expects one argument but no return value. */
		return xlretSuccess;
	default:;
	}
	return xlretInvXlfn; /* "An invalid function number was supplied. If you are using constants from XLCALL.H, this
				 should not occur unless you are calling something that is not supported in the version
				 of Excel you are running." */
}

static void
load_xlcall32 (GOPlugin *plugin)
{
	gchar *full_module_file_name;
	if (!g_module_supported ()) {
		g_warning (_("Dynamic module loading is not supported on this system."));
		return;
	}
	full_module_file_name = g_build_filename (go_plugin_get_dir_name (plugin), "xlcall32", NULL);
#ifdef WIN32
	SetErrorMode (SEM_FAILCRITICALERRORS); /* avoid message box if library not found */
#endif
	xlcall32_handle = g_module_open (full_module_file_name, G_MODULE_BIND_LAZY);
#ifdef WIN32
	SetErrorMode (0);
#endif
	if (xlcall32_handle == NULL) {
		g_warning (_("Unable to open module file \"%s\"."),full_module_file_name);
		return;
	}
	g_module_symbol (xlcall32_handle, "register_actual_excel4v", (gpointer) &register_actual_excel4v);
	if (register_actual_excel4v == NULL) {
		g_warning (_("Module \"%s\" doesn't contain (\"register_actual_excel4v\" symbol)."),full_module_file_name);
		return;
	}
	register_actual_excel4v (actual_Excel4v);
	g_free (full_module_file_name);
}

static void
scan_for_XLLs_and_register_functions (const gchar *dir_name)
{
	GDir *dir = g_dir_open (dir_name, 0, NULL);
	const gchar *d_name;

	if (NULL == dir)
		return;

	while ((d_name = g_dir_read_name (dir)) != NULL) {
		gchar *full_entry_name;
		int stat_success;
		GStatBuf d_info;

		if (strcmp (d_name, ".") == 0 || strcmp (d_name, "..") == 0)
			continue;

		full_entry_name = g_build_filename (dir_name, d_name, NULL);
		stat_success = g_stat (full_entry_name, &d_info);
		if (0 == stat_success) {
			if (S_ISDIR (d_info.st_mode)) {
				scan_for_XLLs_and_register_functions (full_entry_name);
			} else {
				GModule *handle;
#ifdef WIN32
				SetErrorMode (SEM_FAILCRITICALERRORS); /* avoid message box if library not found */
#endif
				handle = g_module_open (full_entry_name, G_MODULE_BIND_LAZY);
#ifdef WIN32
				SetErrorMode (0);
#endif
				if (NULL != handle) {
					XLL*xll = ALLOC_ARRAY (XLL,1);
					XLAutoOpenFunc xlAutoOpenFunc = NULL;
					xll->name   = g_strdup (full_entry_name);
					xll->handle = handle;
					g_module_symbol (xll->handle, "xlAutoFree", (gpointer) &xll->xlAutoFree);
					xlAutoOpenFunc = NULL;
					if (g_module_symbol (xll->handle, "xlAutoOpen", (gpointer) &xlAutoOpenFunc) && xlAutoOpenFunc) {
						currently_called_xll = xll;
						xlAutoOpenFunc ();
						currently_called_xll = NULL;
						if (0 == xll->number_of_functions) {
							g_warning (_("No loadable worksheet functions found in XLL/DLL/SO file %s."),full_entry_name);
						} else {
							GO_SLIST_PREPEND (XLLs,xll);
							/* xgettext : %lu gives the number of functions. This is input to ngettext. */
							g_message (ngettext("Loaded %lu function from XLL/DLL/SO %s.",
									    "Loaded %lu functions from XLL/DLL/SO %s.",
									    xll->number_of_functions),
								   xll->number_of_functions, full_entry_name);
						}
					}
					if (0 == xll->number_of_functions) {
						free_XLL (xll);
					}
				}
			}
		}
		g_free (full_entry_name);
	}
	g_dir_close (dir);
}

G_MODULE_EXPORT void
go_plugin_init (GOPlugin *plugin, GOCmdContext *cc)
{
	load_xlcall32 (plugin);

	if (NULL == xlcall32_handle) /* If we couldn't load the helper dll, there is no point in continuing. */
		return;

	/*
	 * Find all external XLL functions that are to be adapted into
	 * Gnumeric. We scan for all shared libraries that expose
	 * xlAutoOpen, in this directory, and all sub
	 * directories. Find them, load them, find xlAutoFree if
	 * present, call xlAutoOpen, and record via the exposed
	 * actual_Excel4v function what worksheet functions they want
	 * to register with Excel.
	 *
	 * We search recursively the directory in which this plugin
	 * resides and all of its subdirectories. This is not for any
	 * philosophical reasons and can be changed as desired.
	 */

	scan_for_XLLs_and_register_functions (go_plugin_get_dir_name (plugin));
}

G_MODULE_EXPORT void
go_plugin_shutdown (GOPlugin *plugin, GOCmdContext *cc)
{
	/*
	 * To be formally correct, we should unregister all of the
	 * loaded XLL functions. To write this code is a lot of
	 * effort, and in all likelihood, it is never
	 * needed. Postponed until it becomes really necessary. In
	 * practice, XLL writers usually don't call the xlfUnregister
	 * procedure in Excel for each of the registered functions
	 * either.
	 */

	if (NULL != xll_function_info_map) {
		g_tree_destroy (xll_function_info_map);
		xll_function_info_map = NULL;
	}

	g_slist_free_full (XLLs, free_XLL);
	XLLs = NULL;

	if (register_actual_excel4v)
		register_actual_excel4v (NULL);
	register_actual_excel4v = NULL;

	if (NULL != xlcall32_handle)
		g_module_close (xlcall32_handle);
	xlcall32_handle = NULL;
}

/***************************************************************************/
