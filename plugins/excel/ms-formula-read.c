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

#include "ms-formula.h"

void ms_excel_parse_formular (MS_EXCEL_SHEET *sheet, BIFF_QUERY *q)
{
  BYTE *cur ;
  int length ;

  g_assert (q->ls_op == BIFF_FORMULA) ;
  printf ("Formular:\n") ;
  dump (q->data, q->length) ;
  printf ("Formula at [%d, %d] XF %d :\n", BIFF_GETCOL(q), BIFF_GETROW(q),
	  BIFF_GETXF(q)) ;
  
  length = BIFF_GETWORD(q->data + 20) ;
  cur = q->data + 22 ;
  while (length>0)
    {
      if (BIFF_GETBYTE(cur) > FORMULA_PTG_MAX)
	break ;
      switch (BIFF_GETBYTE(cur))
	{
	case FORMULA_PTG_REF:
	  {
	    int row, col, relrow, relcol ;
	    g_assert (sheet->ver == eBiffV8) ;
	    col = BIFF_GETWORD(cur+1) ;
	    row = BIFF_GETWORD(cur+3) ;
	    relrow = row&0x8000 ;
	    relcol = row&0x4000 ;
	    row    = row&0x3fff ;
	    printf ("Cell [%d,%d] PTG ref! relative col? %d, row?%d\n", col, row, relcol, relrow) ;
	  }
	  break ;
	default:
	  length = 0 ;
	  break ;
	}
    }
  printf ("Unknown PTG 0x%x\n", BIFF_GETBYTE(cur)) ;
  ms_excel_sheet_insert (sheet, BIFF_GETXF(q), BIFF_GETCOL(q), BIFF_GETROW(q), "Unknown formular") ;
}

