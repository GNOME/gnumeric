/*
 * ms-formula.c: MS Excel -> Gnumeric formula conversion
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 */

#include <fcntl.h>
#include <assert.h>
#include <config.h>
#include <stdio.h>
#include <gnome.h>
#include "gnumeric.h"

#include "utils.h"

#include "ms-excel.h"
#include "ms-excel-biff.h"
#include "ms-formula.h"

#define NO_PRECEDENCE 256

/**
 * Various bits of data for operators
 * see S59E2B.HTM for formula_ptg values
 * formula PTG, prefix, middle, suffix, precedence
 **/
FORMULA_OP_DATA formula_op_data[] = {
  /* Binary operator tokens */
  { 0x03, 1, "+",  30  }, /* ptgAdd : Addition */
  { 0x04, 1, "-",  30  }, /* ptgSub : Subtraction */
  { 0x05, 1, "*",  48 }, /* ptgMul : Multiplication */
  { 0x06, 1, "/",  32 }, /* ptgDiv : Division */
  { 0x07, 1, "^",  60 }, /* ptgPower : Exponentiation */
  { 0x08, 1, "&",  28 }, /* ptgConcat : Concatenation */
  { 0x09, 1, "<",  24 }, /* ptgLT : Less Than */
  { 0x0a, 1, "<=", 24 }, /* ptgLTE : Less Than or Equal */
  { 0x0b, 1, "=",  20 }, /* ptgEQ : Equal */
  { 0x0c, 1, ">=", 24 }, /* ptgGTE : Greater Than or Equal */
  { 0x0d, 1, ">" , 24 }, /* ptgGT : Greater Than */
  { 0x0e, 1, "<>", 20 }, /* ptgNE : Not Equal */
  { 0x0f, 1, " " , 62 }, /* ptgIsect : Intersection */
  { 0x10, 1, "," , 62 }, /* ptgUnion : Union */
  { 0x11, 1, ":" , 63 }, /* ptgRange : Range */
  /* Unary operator tokens */
  { 0x12, 0, "+",  64 }, /* ptgUplus : Unary Plus */
  { 0x13, 0, "-",  64 }, /* ptgUminux : Unary Minus */
  { 0x14, 0, "%",  64 }  /* ptgPercent : Percent Sign */
} ;
#define FORMULA_OP_DATA_LEN   (sizeof(formula_op_data)/sizeof(FORMULA_OP_DATA))

/* FIXME: the standard function indexes need to be found from xlcall.h */
/**
 * Various bits of data for functions
 * function index, prefix, middle, suffix, multiple args?, precedence
 **/
FORMULA_FUNC_DATA formula_func_data[] =
{
  { 0x04, "SUM (", 0,  ")", -1, 0 },
  { 0x05, "AVERAGE (", 0,  ")", -1, 0 },
  { 0x06, "MIN (", 0,  ")", -1, 0 },
  { 0x07, "MAX (", 0,  ")", -1, 0 },
  { 0x24, "AND (", 0,  ")", -1, 0 },
  { 0x25, "OR (",  0,  ")", -1, 0 },
  { 0x63, "ACOS (", 0, ")", 1, 0 },
  { 0xe9, "ACOSH (", 0, ")", 1, 0 },
  { 0x62, "ASIN (", 0, ")", 1, 0 },
  { 0xe8, "ASINH (", 0, ")", 1, 0 },
  { 0x12, "ATAN (", 0, ")", 1, 0 },
  { 0x61, "ATAN2 (", 0, ")", 2, 0 },
  { 0xea, "ATANH (", 0, ")", 1, 0 },
  { 0x120, "CEILING (", 0, ")", 2, 0 },
  { 0x10, "COS (", 0, ")", 1, 0 },
  { 0xe6, "COSH (", 0, ")", 1, 0 },
  { 0x15, "EXP (", 0, ")", 1, 0 },
  { 0x16, "LN (", 0, ")", 1, 0 },
  { 0x6d, "LOG (", 0, ")", 1, 0 },
  { 0x17, "LOG10 (", 0, ")", 1, 0 },
  { 0x156, "RADIANS (", 0, ")", 1, 0 },
  { 0x3f, "RAND (", 0, ")", 0, 0 },
  { 0x1a, "SIGN (", 0, ")", 1, 0 },
  { 0xf, "SIN (", 0, ")", 1, 0 },
  { 0xe5, "SINH (", 0, ")", 1, 0 },
  { 0x14, "SQRT (", 0, ")", 1, 0 },
  { 0x11, "TAN (", 0, ")", 1, 0 },
  { 0xe7, "TANH (", 0, ")", 1, 0 },
  { 0xc5, "TRUNC (", 0, ")", 1, 0 },
  { 0x27, "MOD (", 0, ")", 2, 0 },
  { 0x65, "HLOOKUP (", 0, ")", -1, 0 },
  { 0x66, "VLOOKUP (", 0, ")", -1, 0 },
  { 0x71, "UPPER (",   0, ")", 1, 0 }
};
#define FORMULA_FUNC_DATA_LEN (sizeof(formula_func_data)/sizeof(FORMULA_FUNC_DATA))

/**
 *  A useful routine for extracting data from a common
 * storage structure.
 **/
static CellRef *getRefV7(MS_EXCEL_SHEET *sheet, BYTE col, WORD gbitrw, int curcol, int currow)
{
  CellRef *cr = (CellRef *)g_malloc(sizeof(CellRef)) ;
  cr->col          = col ;
  cr->row          = (gbitrw & 0x3fff) ;
  cr->row_relative = (gbitrw & 0x8000)==0x8000 ;
  cr->col_relative = (gbitrw & 0x4000)==0x4000 ;
  if (cr->row_relative)
    cr->row-= currow ;
  if (cr->col_relative)
    cr->col-= curcol ;
  cr->sheet = sheet->gnum_sheet ;
  /*  printf ("7Out : %d, %d  at %d, %d\n", cr->col, cr->row, curcol, currow) ; */
  return cr ;
}
/**
 *  A useful routine for extracting data from a common
 * storage structure.
 **/
static CellRef *getRefV8(MS_EXCEL_SHEET *sheet, WORD row, WORD gbitcl, int curcol, int currow)
{
  CellRef *cr = (CellRef *)g_malloc(sizeof(CellRef)) ;
  cr->row          = row ;
  cr->col          = (gbitcl & 0x3fff) ;
  cr->row_relative = (gbitcl & 0x8000)==0x8000 ;
  cr->col_relative = (gbitcl & 0x4000)==0x4000 ;
  if (cr->row_relative)
    cr->row-= currow ;
  if (cr->col_relative)
    cr->col-= curcol ;
  cr->sheet = sheet->gnum_sheet ;
  /*  printf ("8Out : %d, %d  at %d, %d\n", cr->col, cr->row, curcol, currow) ; */
  return cr ;
}

typedef struct _PARSE_DATA
{
  char *name ;
  int  precedence ;
} PARSE_DATA ;

static PARSE_DATA *parse_data_new (char *buffer, int precedence)
{
  PARSE_DATA *ans = (PARSE_DATA *)g_malloc(sizeof(PARSE_DATA)) ;
  ans->name = buffer ;
  ans->precedence = precedence ;
  return ans ;
}

static void parse_data_free (PARSE_DATA *ptr)
{
  if (ptr->name)
    g_free (ptr->name) ;
  g_free (ptr) ;
}

typedef struct _PARSE_LIST
{
  GList *data ;
  int   length ;
} PARSE_LIST ;

static PARSE_LIST *parse_list_new ()
{
  PARSE_LIST *ans = (PARSE_LIST *)g_malloc (sizeof(PARSE_LIST)) ;
  ans->data   = 0 ;
  ans->length = 0 ;
  return ans ;
}

static void parse_list_push (PARSE_LIST *list, PARSE_DATA *pd)
{
  /*  printf ("Pushing '%s'\n", pd->name) ; */
  list->data = g_list_append (list->data, pd) ;
  list->length++ ;
}

static void parse_list_push_raw (PARSE_LIST *list, char *buffer, int precedence)
{
  parse_list_push(list, parse_data_new (buffer, precedence)) ;
}

static PARSE_DATA *parse_list_pop (PARSE_LIST *list)
{
  GList *tmp ;
  PARSE_DATA *ans ;
  tmp   = g_list_last (list->data) ;
  if (tmp == 0)
    printf ("Warning not enough arguments on stack\n") ;
  list->data = g_list_remove_link (list->data, tmp) ;
  ans  = tmp->data ;
  g_list_free (tmp) ;
  list->length-- ;
  return ans ;
}

static void parse_list_free (PARSE_LIST *list)
{
  while (list->data)
    parse_data_free (parse_list_pop(list)) ;
}

static PARSE_DATA *parse_list_pop_front (PARSE_LIST *list)
{
  PARSE_DATA *ans ;
  GList *tmp ;
  tmp   = g_list_first (list->data) ;
  if (tmp == 0)
    printf ("Warning not enough arguments on stack\n") ;
  list->data = g_list_remove_link (list->data, tmp) ;
  ans  = tmp->data ;
  g_list_free (tmp) ;
  list->length-- ;
  return ans ;
}

/**
 * This pops a load of arguments off the stack,
 * comman delimits them sticking result in 'into'
 * frees the stack space.
 **/
static void 
parse_list_comma_delimit_n (PARSE_LIST *stack, char *prefix,
					int n, char *suffix, int precedence)
{
	char **args, *ans, *put ;
	guint32 slen = 0 ;
	int lp ;

	args = g_new (char *, n) ;

	for (lp=0;lp<n;lp++)
	{
		PARSE_DATA  *dat = parse_list_pop (stack) ;
		args[n-lp-1] = dat->name ;
		slen += dat->name?strlen(dat->name):0 ;
		g_free (dat) ;
	}
	slen+= prefix?strlen(prefix):0 ;
	slen+= suffix?strlen(suffix):0 ;
	slen+= 1 + n ; /* Commas and termination */

	ans = g_new (char, slen) ;

	strcpy (ans, prefix) ;
	put = ans + strlen(ans) ;

	for (lp=0;lp<n;lp++)
		put += sprintf (put, "%c%s", lp?',':' ', args[lp]) ;

	strcat (put, suffix) ;

	parse_list_push_raw (stack, ans, precedence) ;
}

/**
 * Prepends an '=' and handles nasty cases
 **/
static char *parse_list_to_equation (PARSE_LIST *list)
{
  if (list->length > 0 && list->data)
    {
      PARSE_DATA *pd ;
      char *formula ;
      
      pd = g_list_first (list->data)->data ;
      if (!pd)
	return g_strdup ("Stack too short") ;
      if (!pd->name)
	return g_strdup ("No data in stack entry") ;
      if (list->length>1)
	return g_strdup ("Too much data on stack\n") ;

      formula = (char *)g_malloc(strlen(pd->name)+2) ;
      if (!formula)
	return g_strdup ("Out of memory") ;

      strcpy (formula, "=") ;
      strcat (formula, pd->name) ;
/*      printf ("Formula : '%s'\n", formula) ; */
      return formula ;
    }
  else
    return g_strdup ("Nothing on stack") ;
}

/**
 * Should be in cell.c ?
 **/
static Cell *
duplicate_formula (Sheet *sheet, int src_col, int src_row, int dest_col, int dest_row)
{
  Cell *ref_cell, *new_cell;
  
  ref_cell = sheet_cell_get (sheet, src_col, src_row);
  if (!ref_cell || !ref_cell->parsed_node)
      return 0;
  
  new_cell = sheet_cell_new (sheet, dest_col, dest_row);
  cell_set_formula_tree_simple (new_cell, ref_cell->parsed_node);
  return new_cell ;
}

static char *
get_inter_sheet_ref (MS_EXCEL_WORKBOOK *wb, guint16 extn_idx)
{
	char *ans, *first, *last ;

	first = biff_get_externsheet_name (wb,
					   extn_idx, 1) ;
	last  = biff_get_externsheet_name (wb,
					   extn_idx, 0) ;
	ans = g_malloc (first?strlen(first):0 + last?strlen(last):0 + 3) ;
	ans[0] = 0 ;
	if (first!=last && first)
	{
		strcat (ans, first) ;
		strcat (ans, ":") ;
	}
	if (last)
	{
		strcat (ans, last) ;
		strcat (ans, "!") ;
	}
	return ans ;
}

static gboolean
make_function (PARSE_LIST *stack, int fn_idx, int numargs)
{
	int lp ;
			
	for (lp=0;lp<FORMULA_FUNC_DATA_LEN;lp++)
	{
		if (formula_func_data[lp].function_idx == fn_idx)
		    {
			const FORMULA_FUNC_DATA *fd = &formula_func_data[lp] ;
			
			if (fd->num_args != -1)
				numargs = fd->num_args ;

			parse_list_comma_delimit_n (stack, (fd->prefix)?fd->prefix:"",
						    numargs,
						    fd->suffix?fd->suffix:"", fd->precedence) ;
			return 1 ;
		}
	}
	if (lp==FORMULA_FUNC_DATA_LEN)
		printf ("FIXME, unimplemented fn 0x%x, with %d args\n", fn_idx, numargs) ;
	return 0 ;
}

/**
 * Parse that RP Excel formula, see S59E2B.HTM
 * Sadly has to be parsed to text and back !
 **/
void ms_excel_parse_formula (MS_EXCEL_SHEET *sheet, BIFF_QUERY *q,
			     int fn_col, int fn_row)
{
	Cell *cell ;
	BYTE *cur ;
	int length ;
	PARSE_LIST *stack ;
	int error = 0 ;
	char *ans ;
	int array_col_first, array_col_last ;
	int array_row_first, array_row_last ;
	int fn_xf ;
	
	if (q->ls_op == BIFF_FORMULA)
	{
		fn_xf           = EX_GETXF(q) ;
/*      printf ("Formula at [%d, %d] XF %d :\n", fn_col, fn_row, fn_xf) ;
	printf ("formula data : \n") ;
	dump (q->data +22, q->length-22) ; */
		/* This will be safe when we collate continuation records in get_query */
		length = BIFF_GETWORD(q->data + 20) ;
		/* NB. the effective '+1' here is so that the offsets and lengths
		   are identical to those in the documentation */
		cur = q->data + 22 + 1 ;
		array_col_first = fn_col ;
		array_col_last  = fn_col ;
		array_row_first = fn_row ;
		array_row_last  = fn_row ;
	}
	else
	{
		g_assert (q->ls_op == BIFF_ARRAY) ;
		fn_xf = 0 ;
/*      printf ("Array at [%d, %d] XF %d :\n", fn_col, fn_row, fn_xf) ;
	printf ("Array data : \n") ;
	dump (q->data +22, q->length-22) ; */
		/* This will be safe when we collate continuation records in get_query */
		length = BIFF_GETWORD(q->data + 12) ;
		/* NB. the effective '+1' here is so that the offsets and lengths
		   are identical to those in the documentation */
		cur = q->data + 14 + 1 ;
		array_row_first = BIFF_GETWORD(q->data + 0) ;
		array_row_last  = BIFF_GETWORD(q->data + 2) ;
		array_col_first = BIFF_GETBYTE(q->data + 4) ;
		array_col_last  = BIFF_GETBYTE(q->data + 5) ;
	}
	
	stack = parse_list_new() ;      
	while (length>0 && !error)
	{
		int ptg_length = 0 ;
		int ptg = BIFF_GETBYTE(cur-1) ;
		int ptgbase = ((ptg & 0x40) ? (ptg | 0x20): ptg) & 0x3F ;
		if (ptg > FORMULA_PTG_MAX)
			break ;
		switch (ptgbase)
		{
		case FORMULA_PTG_REF:
		{
			CellRef *ref ;
			char *buffer ;
			if (sheet->ver == eBiffV8)
			{
				ref = getRefV8 (sheet, BIFF_GETWORD(cur), BIFF_GETWORD(cur + 2), fn_col, fn_row) ;
				ptg_length = 4 ;
			}
			else
			{
				ref = getRefV7 (sheet, BIFF_GETBYTE(cur+2), BIFF_GETWORD(cur), fn_col, fn_row) ;
				ptg_length = 3 ;
			}
			buffer = cellref_name (ref, sheet->gnum_sheet, fn_col, fn_row) ;
			parse_list_push_raw (stack, buffer, NO_PRECEDENCE) ;
/*	    printf ("%s\n", buffer) ; */
			g_free (ref) ;
		}
		break ;
		case FORMULA_PTG_REF_3D: /* see S59E2B.HTM */
		{
			CellRef *ref=0 ;
			char *buffer ;
			
			if (sheet->ver == eBiffV8)
			{
				guint16 extn_idx = BIFF_GETWORD(cur) ;
				char ans[4096], *intertxt, *ptr ; /* FIXME */

				intertxt = get_inter_sheet_ref (sheet->wb, extn_idx) ;
				strcpy (ans, intertxt) ;
				g_free (intertxt) ;
				ref = getRefV8 (sheet, BIFF_GETWORD(cur+2), BIFF_GETWORD(cur + 4), fn_col, fn_row) ;
				strcat (ans, (ptr = cellref_name (ref, sheet->gnum_sheet, fn_col, fn_row))) ;
				g_free (ptr) ;
				printf("Answer : '%s'\n", ans) ;
				parse_list_push_raw (stack, g_strdup(ans), NO_PRECEDENCE) ;
				ptg_length = 6 ;
			}
			else
			{
				printf ("FIXME: Biff V7 3D refs are ugly !\n") ;
				error = 1 ;
				ref = 0 ;
				ptg_length = 16 ;
			}
			if (ref)
				g_free (ref) ;
		}
		break ;
		case FORMULA_PTG_AREA_3D: /* see S59E2B.HTM */
		{
			CellRef *ref=0 ;
			char *buffer ;
			
			if (sheet->ver == eBiffV8)
			{
				guint16 extn_idx = BIFF_GETWORD(cur) ;
				char *intertxt ;
				char buffer[4096], *ptr ;  /* FIXME */
				CellRef *first, *last ;

				intertxt = get_inter_sheet_ref (sheet->wb, extn_idx) ; 
				strcpy (buffer, intertxt) ;
				g_free (intertxt) ;

				first = getRefV8(sheet, BIFF_GETBYTE(cur+2), BIFF_GETWORD(cur+6), fn_col, fn_row) ;
				last  = getRefV8(sheet, BIFF_GETBYTE(cur+4), BIFF_GETWORD(cur+8), fn_col, fn_row) ;

				strcat (buffer, (ptr = cellref_name (first, sheet->gnum_sheet, fn_col, fn_row))) ;
				g_free (ptr) ;
				strcat (buffer, ":") ;
				strcat (buffer, (ptr=cellref_name (last, sheet->gnum_sheet, fn_col, fn_row))) ;
				g_free (ptr) ;
				parse_list_push_raw(stack, g_strdup (buffer), NO_PRECEDENCE) ;
				g_free (first) ;
				g_free (last) ;
				
				ptg_length = 10 ;
			}
			else
			{
				printf ("FIXME: Biff V7 3D refs are ugly !\n") ;
				error = 1 ;
				ref = 0 ;
				ptg_length = 20 ;
			}
			if (ref)
				g_free (ref) ;
		}
		break ;
		case FORMULA_PTG_AREA:
		{
			CellRef *first, *last ;
			char buffer[64] ;
			char *ptr ;
			if (sheet->ver == eBiffV8)
			{
				first = getRefV8(sheet, BIFF_GETBYTE(cur+0), BIFF_GETWORD(cur+4), fn_col, fn_row) ;
				last  = getRefV8(sheet, BIFF_GETBYTE(cur+2), BIFF_GETWORD(cur+6), fn_col, fn_row) ;
				ptg_length = 8 ;
			}
			else
			{
				first = getRefV7(sheet, BIFF_GETBYTE(cur+4), BIFF_GETWORD(cur+0), fn_col, fn_row) ;
				last  = getRefV7(sheet, BIFF_GETBYTE(cur+5), BIFF_GETWORD(cur+2), fn_col, fn_row) ;
				ptg_length = 6 ;
			}
			strcpy (buffer, (ptr = cellref_name (first, sheet->gnum_sheet, fn_col, fn_row))) ;
			g_free (ptr) ;
			strcat (buffer, ":") ;
			strcat (buffer, (ptr=cellref_name (last, sheet->gnum_sheet, fn_col, fn_row))) ;
			g_free (ptr) ;
			parse_list_push_raw(stack, g_strdup (buffer), NO_PRECEDENCE) ;
/*	    printf ("%s\n", buffer) ; */
			g_free (first) ;
			g_free (last) ;
		}
		break ;
		case FORMULA_PTG_FUNC:
		{
			if (!make_function (stack, BIFF_GETWORD(cur), -1)) error = 1 ;
			ptg_length = 2 ;
			break ;
		}
		case FORMULA_PTG_FUNC_VAR:
		{
			int numargs = (BIFF_GETBYTE( cur ) & 0x7f) ;
			int prompt  = (BIFF_GETBYTE( cur ) & 0x80) ;   /* Prompts the user ?  */
			int iftab   = (BIFF_GETWORD(cur+1) & 0x7fff) ; /* index into fn table */
			int cmdquiv = (BIFF_GETWORD(cur+1) & 0x8000) ; /* is a command equiv.?*/

			if (!make_function (stack, iftab, numargs)) error = 1 ;
			ptg_length = 3 ;
		}
		break ;
		case FORMULA_PTG_EXP: /* FIXME: the formula is the same as another record ... we need a cell_get_funtion call ! */
		{
			cell = sheet_cell_fetch (sheet->gnum_sheet, fn_col, fn_row) ;
			if (!cell->text) /* FIXME: work around cell.c bug, we can't have formatting with no text in a cell ! */
				cell_set_text_simple(cell, "") ;
			ms_excel_set_cell_xf (sheet, cell, fn_xf) ;
			return ;
		}
		break ;
		case FORMULA_PTG_PAREN:
/*	  printf ("Ignoring redundant parenthesis ptg\n") ; */
			ptg_length = 0 ;
			break ;
		case FORMULA_PTG_MISSARG:
			parse_list_push_raw (stack, g_strdup (""), NO_PRECEDENCE) ;
			ptg_length = 0 ;
			break ;
		case FORMULA_PTG_ATTR: /* FIXME: not fully implemented */
		{
			guint8  grbit = BIFF_GETBYTE(cur) ;
			guint16 w     = BIFF_GETWORD(cur+1) ;
			ptg_length = 3 ;
			if (grbit & 0x40) /* AttrSpace */
			{
				guint8 num_space = BIFF_GETBYTE(cur+2) ;
				guint8 attrs     = BIFF_GETBYTE(cur+1) ;
				if (attrs == 00) /* bitFSpace : ignore it */
				/* Could perhaps pop top arg & append space ? */ ;
				else
					printf ("Redundant whitespace in formula 0x%x count %d\n", attrs, num_space) ;
			}
			else
				printf ("Unknown PTG Attr 0x%x 0x%x\n", grbit, w) ;
		break ;
		}
		case FORMULA_PTG_INT:
		{
			char buf[8]; /* max. "65535" */
			guint16 num = BIFF_GETWORD(cur) ;
			sprintf(buf,"%u",num);
			parse_list_push_raw (stack, g_strdup(buf), NO_PRECEDENCE) ;
			ptg_length = 2 ;
			break;
		}
		case FORMULA_PTG_BOOL:
		{
			parse_list_push_raw (stack, g_strdup(BIFF_GETBYTE(cur) ? "TRUE" : "FALSE" ), NO_PRECEDENCE) ;
			ptg_length = 1 ;
			break ;
		}
		case FORMULA_PTG_NUM:
		{
			double tmp = BIFF_GETDOUBLE(cur) ;
			char buf[65] ; /* should be long enough? */
			snprintf (buf, 64, "%f", tmp) ;
			parse_list_push_raw (stack, g_strdup(buf), NO_PRECEDENCE) ;
			ptg_length = 8 ;
			break ;
		}
		case FORMULA_PTG_STR:
		{ /* FIXME: Len should only be a byte but seems to be a word ! */
			char *str ;
			printf ("PTG_STR\n") ;
			dump (q->data, q->length) ;
			str = biff_get_text (cur+2, BIFF_GETBYTE(cur)) ;
			parse_list_push_raw (stack, str, NO_PRECEDENCE) ;
			printf ("Found string '%s'\n", str) ; 
			ptg_length = 1 + BIFF_GETBYTE(cur) ;
			break ;
		}
		default:
		{
			int lp ;
			
/*	    printf ("Search %d records\n", (int)FORMULA_OP_DATA_LEN) ; */
			for (lp=0;lp<FORMULA_OP_DATA_LEN;lp++)
			{
				if (ptgbase == formula_op_data[lp].formula_ptg)
				{
					PARSE_DATA *arg1=0, *arg2 ;
					GList *tmp ;
					char buffer[2048] ;
					FORMULA_OP_DATA *fd = &formula_op_data[lp] ;
					int bracket_arg2 ;
					int bracket_arg1 ;
					
					buffer[0] = '\0' ; /* Terminate string */
					arg2 = parse_list_pop (stack) ;
					bracket_arg2 = arg2->precedence<fd->precedence ;
					if (fd->infix)
					{
						arg1 = parse_list_pop (stack) ;
						bracket_arg1 = arg1->precedence<fd->precedence ; 
						if (bracket_arg1)
							strcat (buffer, "(") ;
						strcat (buffer, arg1->name) ;
						if (bracket_arg1)
							strcat (buffer, ")") ;
					}
					strcat (buffer, fd->mid?fd->mid:"") ;
					if (bracket_arg2)
						strcat (buffer, "(") ;
					strcat (buffer, arg2->name) ;
					if (bracket_arg2)
						strcat (buffer, ")") ;
					
/*		    printf ("Op : '%s'\n", buffer) ; */
					parse_list_push_raw(stack, g_strdup (buffer), fd->precedence) ;
					if (fd->infix)
						parse_data_free (arg1) ;
					parse_data_free (arg2) ;
					break ;
				}
			}
			if (lp==FORMULA_OP_DATA_LEN)
				printf ("Unknown PTG 0x%x base %x\n", ptg, ptgbase), error=1 ;
		}
		}
/*      printf ("Ptg 0x%x length (not inc. ptg byte) %d\n", ptgbase, ptg_length) ; */
		cur+=    (ptg_length+1) ;
		length-= (ptg_length+1) ;
	}
	if (error)
	{
		int xlp, ylp ;

		printf ("Unknown Formula/Array at [%d, %d] XF %d :\n", fn_col, fn_row, fn_xf) ;
		printf ("formula data : \n") ;
		dump (q->data +22, q->length-22) ;
		
		for (xlp=array_col_first;xlp<=array_col_last;xlp++)
			for (ylp=array_row_first;ylp<=array_row_last;ylp++)
				ms_excel_sheet_insert (sheet, fn_xf, xlp, ylp, "Unknown formula") ;
		parse_list_free (stack) ;
		return ;
	}
	
	ans = parse_list_to_equation (stack) ;
	if (ans)
	{
		int xlp, ylp ;
		for (xlp=array_col_first;xlp<=array_col_last;xlp++)
			for (ylp=array_row_first;ylp<=array_row_last;ylp++)
			{
				cell = sheet_cell_fetch (sheet->gnum_sheet, EX_GETCOL(q), EX_GETROW(q)) ;
				/* FIXME: this _should_ be a set_formula with the formula, and a
				   set_text_simple with the current value */
				cell_set_text (cell, ans) ;
				ms_excel_set_cell_xf (sheet, cell, EX_GETXF(q)) ;
			}
		g_free (ans) ;
	}
	parse_list_free (stack) ;
}

void ms_excel_fixup_array_formulae (MS_EXCEL_SHEET *sheet)
{
	GList *tmp = sheet->array_formulae ;
	while (tmp)
	{
		FORMULA_ARRAY_DATA *dat = tmp->data ;
		printf ("Copying formula from %d,%d to %d,%d\n",
			dat->src_col, dat->src_row,
			dat->dest_col, dat->dest_row) ;
		duplicate_formula (sheet->gnum_sheet,
				   dat->src_col, dat->src_row,
				   dat->dest_col, dat->dest_row) ;
		tmp = tmp->next ;
	}
}
