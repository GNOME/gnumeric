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
  { 0x03, 0, "+", 0, 8 },
  { 0x04, 0, "-", 0, 8 },
  { 0x05, 0, "*", 0, 32 },
  { 0x06, 0, "/", 0, 16 }
} ;
#define FORMULA_OP_DATA_LEN   (sizeof(formula_op_data)/sizeof(FORMULA_OP_DATA))

/* FIXME: the standard function indexes need to be found from xlcall.h */
/**
 * Various bits of data for functions
 * function index, prefix, middle, suffix, multiple args?, precedence
 **/
FORMULA_FUNC_DATA formula_func_data[] =
{
  { 0x04, "SUM (", 0,  ")", 1, 0 },
  { 0x05, "AVERAGE (", 0,  ")", 1, 0 },
  { 0x06, "MIN (", 0,  ")", 1, 0 },
  { 0x07, "MAX (", 0,  ")", 1, 0 }
};
#define FORMULA_FUNC_DATA_LEN (sizeof(formula_func_data)/sizeof(FORMULA_FUNC_DATA))

/**
 *  A useful routine for extracting data from a common
 * storage structure.
 **/
static CellRef *getRefV7(MS_EXCEL_SHEET *sheet, BYTE col, WORD gbitrw, int curcol, int currow)
{
  CellRef *cr = (CellRef *)malloc(sizeof(CellRef)) ;
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
  CellRef *cr = (CellRef *)malloc(sizeof(CellRef)) ;
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
  PARSE_DATA *ans = (PARSE_DATA *)malloc(sizeof(PARSE_DATA)) ;
  ans->name = buffer ;
  ans->precedence = precedence ;
  return ans ;
}

static void parse_data_free (PARSE_DATA *ptr)
{
  if (ptr->name)
    free (ptr->name) ;
  free (ptr) ;
}

typedef struct _PARSE_LIST
{
  GList *data ;
  int   length ;
} PARSE_LIST ;

static PARSE_LIST *parse_list_new ()
{
  PARSE_LIST *ans = (PARSE_LIST *)malloc (sizeof(PARSE_LIST)) ;
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
static void parse_list_comma_delimit_n (PARSE_LIST *list, char *into, int n)
{
  char *buffer = into ;
  int lp, start ;
  GList *ptr ;

  start = list->length - n ;
  ptr = g_list_nth (list->data, start) ;
  for (lp=0;lp<n;lp++)
    {      
      char *str   = &buffer[strlen(buffer)] ;
      PARSE_DATA  *dat = ptr->data ;
      char *appnd = dat->name ;
      ptr = ptr->next ;
      sprintf (str, "%s%c", appnd, ptr?',':' ') ;
    }

  for (lp=0;lp<n;lp++)
    {
      PARSE_DATA *dat = parse_list_pop (list) ;
      parse_data_free (dat) ;
    }
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
	return "Stack too short" ;
      if (!pd->name)
	return "No data in stack entry" ;

      formula = (char *)malloc(strlen(pd->name)+2) ;
      if (!formula)
	return "Out of memory" ;

      strcpy (formula, "=") ;
      strcat (formula, pd->name) ;
      printf ("The answer is : '%s'\n", formula) ;
      return formula ;
    }
  else
    return "Nothing on stack" ;
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
      printf ("Formula at [%d, %d] XF %d :\n", fn_col, fn_row, fn_xf) ;
      printf ("formula data : \n") ;
      dump (q->data +22, q->length-22) ;
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
      printf ("Array at [%d, %d] XF %d :\n", fn_col, fn_row, fn_xf) ;
      printf ("Array data : \n") ;
      dump (q->data +22, q->length-22) ;
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
	    parse_list_push_raw(stack, buffer, NO_PRECEDENCE) ;
	    printf ("%s\n", buffer) ;
	    free (ref) ;
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
	    free (ptr) ;
	    strcat (buffer, ":") ;
	    strcat (buffer, (ptr=cellref_name (last, sheet->gnum_sheet, fn_col, fn_row))) ;
	    free (ptr) ;
	    parse_list_push_raw(stack, strdup (buffer), NO_PRECEDENCE) ;
	    printf ("%s\n", buffer) ;
	    free (first) ;
	    free (last) ;
	  }
	  break ;
	case FORMULA_PTG_FUNC_VAR:
	  {
	    int numargs = (BIFF_GETBYTE( cur ) & 0x7f) ;
	    int prompt  = (BIFF_GETBYTE( cur ) & 0x80) ;   /* Prompts the user ?  */
	    int iftab   = (BIFF_GETWORD(cur+1) & 0x7fff) ; /* index into fn table */
	    int cmdquiv = (BIFF_GETWORD(cur+1) & 0x8000) ; /* is a command equiv.?*/
	    GList *args=0, *tmp ;
	    int lp ;
	    char buffer[4096] ; /* Nasty ! */
	    printf ("Found formula %d with %d args\n", iftab, numargs) ;

	    for (lp=0;lp<FORMULA_FUNC_DATA_LEN;lp++)
	      {
		if (formula_func_data[lp].function_idx == iftab && formula_func_data[lp].multi_arg)
		  {
		    const FORMULA_FUNC_DATA *fd = &formula_func_data[lp] ;
		    GList *ptr ;

		    strcpy (buffer, (fd->prefix)?fd->prefix:"") ;

		    parse_list_comma_delimit_n (stack, &buffer[strlen(buffer)], numargs) ;

		    strcat (buffer, fd->suffix?fd->suffix:"") ;
		    parse_list_push_raw(stack, strdup (buffer), fd->precedence) ;
		    break ;
		  }
	      }
	    if (lp==FORMULA_FUNC_DATA_LEN)
	      printf ("FIXME, unimplemented vararg fn %d, with %d args\n", iftab, numargs), error=1 ;
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
	  printf ("Ignoring redundant parenthesis ptg\n") ;
	  ptg_length = 0 ;
	  break ;
	case FORMULA_PTG_FUNC:
	  {
	    int iftab   = BIFF_GETWORD(cur) ;
	    printf ("FIXME, unimplemented function table pointer %d\n", iftab), error=1 ;
	    ptg_length = 2 ;
	  }
	  break ;
	default:
	  {
	    int lp ;

	    printf ("Search %d records\n", FORMULA_OP_DATA_LEN) ;
	    for (lp=0;lp<FORMULA_OP_DATA_LEN;lp++)
	      {
		if (ptgbase == formula_op_data[lp].formula_ptg)
		  {
		    PARSE_DATA *arg1, *arg2 ;
		    GList *tmp ;
		    char buffer[2048] ;
		    FORMULA_OP_DATA *fd = &formula_op_data[lp] ;
		    int bracket_arg2 ;
		    int bracket_arg1 ;

		    arg2 = parse_list_pop (stack) ;
		    arg1 = parse_list_pop (stack) ;

		    bracket_arg2 = arg2->precedence<fd->precedence ;
		    bracket_arg1 = arg1->precedence<fd->precedence ;

		    strcpy (buffer, fd->prefix?fd->prefix:"") ;
		    if (bracket_arg1)
		      strcat (buffer, "(") ;
		    strcat (buffer, arg1->name) ;
		    if (bracket_arg1)
		      strcat (buffer, ")") ;
		    strcat (buffer, fd->mid?fd->mid:"") ;
		    if (bracket_arg2)
		      strcat (buffer, "(") ;
		    strcat (buffer, arg2->name) ;
		    if (bracket_arg2)
		      strcat (buffer, ")") ;
		    strcat (buffer, fd->suffix?fd->suffix:"") ;

		    printf ("Op : '%s'\n", buffer) ;
		    parse_list_push_raw(stack, strdup (buffer), fd->precedence) ;
		    parse_data_free (arg1) ;
		    parse_data_free (arg2) ;
		    break ;
		  }
	      }
	    if (lp==FORMULA_OP_DATA_LEN)
	      printf ("Unknown PTG 0x%x base %x\n", ptg, ptgbase), error=1 ;
	  }
	}
      cur+=    (ptg_length+1) ;
      length-= (ptg_length+1) ;
    }
  if (error)
    {
      int xlp, ylp ;
      for (xlp=array_col_first;xlp<=array_col_last;xlp++)
	  for (ylp=array_row_first;ylp<=array_row_last;ylp++)
	    ms_excel_sheet_insert (sheet, fn_xf, xlp, ylp, "Unknown formula") ;
      return ;
    }
  printf ("--------- Found valid formula !---------\n") ;
  
  ans = parse_list_to_equation(stack) ;
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


