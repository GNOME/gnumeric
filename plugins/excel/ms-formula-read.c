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
#include "ms-formula.h"

/* FIXME these probably don't work weel with negative numbers ! */
/**
 *  A useful routine for extracting data from a common
 * storage structure.
 **/
static CellRef *getRefV7(BYTE col, WORD gbitrw)
{
  CellRef *cr = (CellRef *)malloc(sizeof(CellRef)) ;
  cr->col          = col ;
  cr->row          = (gbitrw & 0x3fff) ;
  cr->row_relative = (gbitrw & 0x8000) ;
  cr->col_relative = (gbitrw & 0x4000) ;
  return cr ;
}
/**
 *  A useful routine for extracting data from a common
 * storage structure.
 **/
static CellRef *getRefV8(WORD row, WORD gbitcl)
{
  CellRef *cr = (CellRef *)malloc(sizeof(CellRef)) ;
  cr->row          = row ;
  cr->col          = (gbitcl & 0x3fff) ;
  cr->row_relative = (gbitcl & 0x8000) ;
  cr->col_relative = (gbitcl & 0x4000) ;
  return cr ;
}

/**
 * Parse that RP Excel formula, see S59E2B.HTM
 * Sadly has to be parsed to text and back !
 **/
void ms_excel_parse_formula (MS_EXCEL_SHEET *sheet, BIFF_QUERY *q)
{
  Cell *cell ;
  BYTE *cur ;
  int length, fn_row, fn_col ;
  GList *stack = 0 ;   /* A whole load of Text arguments */

  g_assert (q->ls_op == BIFF_FORMULA) ;
  fn_col = BIFF_GETCOL(q) ;
  fn_row = BIFF_GETROW(q) ;
  printf ("Formula at [%d, %d] XF %d :\n", fn_row, fn_col, BIFF_GETXF(q)) ;
  printf ("formula data : \n") ;
  dump (q->data +22, q->length-22) ;
  /* This will be safe when we collate continuation records in get_query */
  length = BIFF_GETWORD(q->data + 20) ;
  /* NB. the effective '-1' here is so that the offsets and lengths
     are identical to those in the documentation */
  cur = q->data + 23 ;
  while (length>0)
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
		ref = getRefV8 (BIFF_GETWORD(cur), BIFF_GETWORD(cur + 2)) ;
		ptg_length = 4 ;
	      }
	    else
	      {
		ref = getRefV7 (BIFF_GETBYTE(cur+2), BIFF_GETWORD(cur)) ;
		ptg_length = 3 ;
	      }
	    buffer = cellref_name (ref, fn_col, fn_row) ;
	    stack = g_list_append (stack, strdup (buffer)) ;
	    printf ("%s\n", buffer) ;
	  }
	  break ;
	case FORMULA_PTG_AREA:
	  {
	    CellRef *first, *last ;
	    char buffer[128] ;
	    if (sheet->ver == eBiffV8)
	      {
		first = getRefV8(BIFF_GETBYTE(cur+0), BIFF_GETWORD(cur+4)) ;
		last  = getRefV8(BIFF_GETBYTE(cur+2), BIFF_GETWORD(cur+6)) ;
		ptg_length = 8 ;
	      }
	    else
	      {
		first = getRefV7(BIFF_GETBYTE(cur+4), BIFF_GETWORD(cur+0)) ;
		last  = getRefV7(BIFF_GETBYTE(cur+5), BIFF_GETWORD(cur+2)) ;
		ptg_length = 6 ;
	      }
	    strcpy (buffer, cellref_name (first, fn_col, fn_row)) ;
	    strcat (buffer, ":") ;
	    strcat (buffer, cellref_name (last, fn_col, fn_row)) ;
	    stack = g_list_append (stack, strdup(buffer)) ;
	    printf ("%s\n", buffer) ;
	  }
	  break ;
	  /* FIXME: the standard function indexes need to be found from xlcall.h */
	case FORMULA_PTG_FUNC_VAR:
	  {
	    int numargs = (BIFF_GETBYTE( cur ) & 0x7f) ;
	    int prompt  = (BIFF_GETBYTE( cur ) & 0x80) ;   /* Prompts the user ?  */
	    int iftab   = (BIFF_GETWORD(cur+1) & 0x7fff) ; /* index into fn table */
	    int cmdquiv = (BIFF_GETWORD(cur+1) & 0x8000) ; /* is a command equiv.?*/
	    GList *args=0, *tmp ;
	    int lp ;
	    char buffer[4096] ; /* Nasty ! */
	    char *ptr ;
	    printf ("Found formula %d with %d args\n", iftab, numargs) ;
	    /* Whack those arguments on an arg list */
	    for (lp=0;lp<numargs;lp++)
	      {
		tmp   = g_list_last (stack) ;
		stack = g_list_remove_link (stack, tmp) ;
		args  = g_list_append(args, tmp->data) ;
		g_list_free (tmp) ;
	      }
	    /* The arguments are now in reverse order in 'args' */
	    if (iftab == 4) /* Guess at a SUM fn */
	      {
		sprintf (buffer, "SUM( ") ;
		tmp = g_list_first (args) ;
		for (lp=0;lp<numargs;lp++)
		  {
		    ptr = &buffer[strlen(buffer)] ;
		    sprintf (ptr, "%s%c", (char *)g_list_nth_data(args, lp), (lp<numargs-1)?',':' ') ;
		  }
		strcat (buffer, ")") ;
		stack = g_list_append (stack, strdup(buffer)) ;
	      }
	    else
	      printf ("FIXME, unimplemented vararg fn %d, with %d args\n", iftab, numargs) ;
	    /* Free args : should be unused by now */
	    tmp = g_list_first (args) ;
	    while (tmp)
	      {
		free (tmp->data) ;
		tmp=tmp->next ;
	      }
	    g_list_free (args) ;
	    ptg_length = 3 ;
	  }
	  break ;
	case FORMULA_PTG_EXP: /* FIXME: the formula is the same as another record ... we need a cell_get_funtion call ! */
	  {
	    int row, col ;
	    row = BIFF_GETWORD(cur) ;
	    col = BIFF_GETWORD(cur+2) ;
	    printf ("Unimplemented ARARY formula at [%d,%d]\n", row, col) ;
	    return ;
	    ptg_length = 4 ;
	  }
	  break ;
	case FORMULA_PTG_FUNC:
	  {
	    int iftab   = BIFF_GETWORD(cur) ;
	    printf ("FIXME, unimplemented function table pointer %d\n", iftab) ;
	    return ;
	    ptg_length = 2 ;
	  }
	  break ;
	default:
	  printf ("Unknown PTG 0x%x base %x\n", ptg, ptgbase) ;
	  ms_excel_sheet_insert (sheet, BIFF_GETXF(q), BIFF_GETCOL(q), BIFF_GETROW(q), "Unknown formula") ;
	  return ;
	}
      cur+=    (ptg_length+1) ;
      length-= (ptg_length+1) ;
    }
  printf ("--------- Found valid formula !---------\n") ;
  cell = sheet_cell_fetch (sheet->gnum_sheet, BIFF_GETCOL(q), BIFF_GETROW(q)) ;
  if (stack)
    {
      char *init = g_list_first (stack)->data ;
      char *ptr = (char *)malloc(strlen(init)+2) ;
      strcpy (ptr, "=") ;
      strcat (ptr, init) ;
      printf ("The answer is : '%s'\n", ptr) ;
      /* FIXME: this _should_ be a set_formula with the formula, and a
	 set_text_simple with the current value */
      cell_set_text(cell, ptr) ;
      /* Set up cell stuff */
      ms_excel_set_cell_xf (sheet, cell, BIFF_GETXF(q)) ;
    }
}
