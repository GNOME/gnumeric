/*
 * ms-excel.c: MS Excel support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 */
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <assert.h>
#include <config.h>
#include <stdio.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnome-xml/tree.h"
#include "gnome-xml/parser.h"
#include "color.h"
#include "sheet-object.h"
#include "style.h"

#include "ms-ole.h"
#include "ms-biff.h"
#include "ms-formula.h"
#include "ms-excel.h"

#define STRNPRINTF(ptr,n) { int xxxlp; printf ("'") ; for (xxxlp=0;xxxlp<(n);xxxlp++) printf ("%c", (ptr)[xxxlp]) ; printf ("'\n") ; }
// This needs proper unicode support !
static char *ms_get_biff_text (BYTE *ptr, int length)
{
  int lp ;
  char *ans = (char *)malloc(sizeof(char)*length+1) ;

  for (lp=0;lp<length;lp++)
    ans[lp] = (char)ptr[lp] ;
  ans[lp] = 0 ;
  return ans ;
}

static BIFF_BOUNDSHEET_DATA *new_biff_boundsheet_data (BIFF_QUERY *q, eBiff_version ver)
{
  BIFF_BOUNDSHEET_DATA *ans = (BIFF_BOUNDSHEET_DATA *)malloc (sizeof(BIFF_BOUNDSHEET_DATA)) ;

  if (ver != eBiffV5 &&     // Testing seems to indicate that Biff5 is compatibile with Biff7 here.
      ver != eBiffV7 &&
      ver != eBiffV8)
    {
      printf ("Unknown BIFF Boundsheet spec. Assuming same as Biff7 FIXME\n") ;
      ver = eBiffV7 ;
    }
  ans->streamStartPos = BIFF_GETLONG(q->data) ;
  switch (BIFF_GETBYTE(q->data+4))
    {
    case 00:
      ans->type = eBiffTWorksheet ;
      break;
    case 01:
      ans->type = eBiffTMacrosheet ;
      break ;
    case 02:
      ans->type = eBiffTChart ;
      break ;
    case 06:
      ans->type = eBiffTVBModule ;
      break;
    default:
      printf ("Unknown sheet type : %d\n", BIFF_GETBYTE(q->data+4)) ;
      ans->type = eBiffTUnknown ;
      break ;
    }
  switch ((BIFF_GETBYTE(q->data+5)) & 0x3)
    {
    case 00:
      ans->hidden = eBiffHVisible ;
      break ;
    case 01:
      ans->hidden = eBiffHHidden ;
      break ;
    case 02:
      ans->hidden = eBiffHVeryHidden ;
      break ;
    default:
      printf ("Unknown sheet hiddenness %d\n", (BIFF_GETBYTE(q->data+4)) & 0x3) ;
      ans->hidden = eBiffHVisible ;
      break ;
    }
  if (ver==eBiffV8)
    {
      int strlen = BIFF_GETWORD(q->data+6) ;
      ans->name = ms_get_biff_text (q->data+8, strlen) ;
    }
  else
    {
      int strlen = BIFF_GETBYTE(q->data+6) ;
      ans->name = ms_get_biff_text (q->data+7, strlen) ;
    }
  printf ("Blocksheet : '%s', %d:%d offset %lx\n", ans->name, ans->type, ans->hidden, ans->streamStartPos) ;
  return ans ;
}

static void free_biff_boundsheet_data (BIFF_BOUNDSHEET_DATA *d)
{
  free (d->name) ;
  free (d) ;
}

static MS_EXCEL_PALETTE *new_ms_excel_palette (BIFF_QUERY *q)
{
  int lp, len ;
  MS_EXCEL_PALETTE *pal ;

  pal = (MS_EXCEL_PALETTE *)malloc (sizeof(MS_EXCEL_PALETTE)) ;
  len = BIFF_GETWORD(q->data) ;
  pal->length = len ;
  pal->red    = (int *)malloc(sizeof(int)*len) ;
  pal->green  = (int *)malloc(sizeof(int)*len) ;
  pal->blue   = (int *)malloc(sizeof(int)*len) ;
  printf ("New palette with %d entries\n", len) ;
  for (lp=0;lp<pal->length;lp++)
    {
      LONG num  = BIFF_GETLONG(q->data+2+lp*4) ;
      pal->red[lp]   = (num & 0x00ff0000) >> 16 ;
      pal->green[lp] = (num & 0x0000ff00) >>  8 ;
      pal->blue[lp]  = (num & 0x000000ff) >>  0 ;
      printf ("Colour %d : (%d,%d,%d)\n", lp, pal->red[lp], pal->green[lp], pal->blue[lp]) ;
    }
  return pal ;
}

typedef struct _BIFF_XF_DATA
{
  WORD             font_idx ;
  WORD             format_idx ;
  eBiff_hidden     hidden ;
  eBiff_locked     locked ;
  eBiff_xftype     xftype ;           // -- Very important field...
  eBiff_format     format ;
  WORD             parentstyle ;
  StyleHAlignFlags halign ;
  StyleVAlignFlags valign ;
  eBiff_wrap       wrap ;
  BYTE             rotation ;
  eBiff_eastern    eastern ;
  BYTE                     border_color[4] ; // Array [eBiff_direction]
  eBiff_border_linestyle   border_line[4] ; // Array [eBiff_direction]
  eBiff_border_orientation border_orientation ;
  eBiff_border_linestyle   border_linestyle ;
  BYTE             fill_pattern_idx ;
  BYTE             foregnd_col ;
  BYTE             backgnd_col ;
} BIFF_XF_DATA ;

static void ms_excel_set_cell_colors (MS_EXCEL_SHEET *sheet, Cell *cell, BIFF_XF_DATA *xf)
{
  MS_EXCEL_PALETTE *p = sheet->wb->palette ;
  int col ;
    
  col = xf->foregnd_col ;
  if (p && col>=0 && col < sheet->wb->palette->length)
    {
      printf ("FG set to %d = (%d,%d,%d)\n", col, p->red[col], p->green[col], p->blue[col]) ;
      cell_set_foreground (cell, p->red[col], p->green[col], p->blue[col]) ;
    }
  else
    printf ("FG col out of range %d\n", col) ;
  col = xf->backgnd_col ;
  if (p && col>=0 && col < sheet->wb->palette->length)
    {
      printf ("BG set to %d = (%d,%d,%d)\n", col, p->red[col], p->green[col], p->blue[col]) ;
      cell_set_background (cell, p->red[col], p->green[col], p->blue[col]) ;
    }
  else
    printf ("BG col out of range %d\n", col) ;
}

static void ms_excel_set_cell_xf(MS_EXCEL_SHEET *sheet, Cell *cell, int xfidx)
{
  GList *ptr ;
  int cnt ;

  if (xfidx == 0)
    {
      printf ("Normal cell formatting\n") ;
      return ;
    }
  if (xfidx == 15)
    {
      printf ("Default cell formatting\n") ;
      return ;
    }
  ptr = g_list_first (sheet->wb->XF_records) ;
  printf ("Looking for %d\n", xfidx) ;
  cnt =  16+5 ; // Magic number ... :-)
  while (ptr)
    {
      BIFF_XF_DATA *xf = ptr->data ;
      if (xf->xftype != eBiffXCell)
	{
	  ptr = ptr->next ;
	  continue ;
	}
      if (cnt == xfidx) // Well set it up then ! FIXME: hack !
	{
	  printf ("Found the style !\n") ;
	  cell_set_alignment (cell, xf->halign, xf->valign, ORIENT_HORIZ, 1) ;
	  ms_excel_set_cell_colors (sheet, cell, xf) ;
	  return ;
	}
      //      printf ("Checking %d\n", cnt) ;
      cnt++ ;
      ptr = ptr->next ;
    }
  printf ("No XF record for %d found :-(\n", xfidx) ;
}

// See S59E1E.HTM !
static BIFF_XF_DATA *new_biff_xf_data (BIFF_QUERY *q, eBiff_version ver)
{
  BIFF_XF_DATA *xf = (BIFF_XF_DATA *)malloc (sizeof(BIFF_XF_DATA)) ;
  LONG data, subdata ;
      
  xf->font_idx  = BIFF_GETWORD(q->data) ;
  xf->format_idx= BIFF_GETWORD(q->data+2) ;

  data          = BIFF_GETWORD(q->data+4) ;
  xf->locked    = (data&0x0001)?eBiffLLocked:eBiffLUnlocked ;
  xf->hidden    = (data&0x0002)?eBiffHHidden:eBiffHVisible ;
  xf->xftype    = (data&0x0004)?eBiffXStyle:eBiffXCell ;
  xf->format    = (data&0x0008)?eBiffFLotus:eBiffFMS ;
  xf->parentstyle = (data>>4) ;
  
  
  data          = BIFF_GETWORD(q->data+6) ;
  subdata       = data&0x0007 ;
  xf->halign    = ( ((subdata == 0) & HALIGN_GENERAL) |
		    ((subdata == 1) & HALIGN_LEFT) |
		    ((subdata == 2) & HALIGN_CENTER) |
		    ((subdata == 3) & HALIGN_RIGHT) |
		    ((subdata == 4) & HALIGN_FILL) |
		    ((subdata == 5) & HALIGN_JUSTIFY)) ;
    //		    ((subdata == 6) & HALIGN_CENTREACROSSSELECTION) |
  xf->wrap      = (data&0x0008)?eBiffWWrap:eBiffWNoWrap ;
  subdata       = (data&0x0070)>>4 ;

  xf->valign    = ( ((subdata == 0) & VALIGN_TOP) |
		    ((subdata == 1) & VALIGN_CENTER) |
		    ((subdata == 2) & VALIGN_BOTTOM) |
		    ((subdata == 3) & VALIGN_JUSTIFY) ) ;
  // FIXME: ignored bit 0x0080
  if (ver == eBiffV8)
      xf->rotation  = (data>>8) ;
  else
    {
      subdata = (data&0x0300)>>8 ;
      xf->rotation  = ( ((subdata == 0) &   0) |
			((subdata == 1) & 255) |   // vertical letters no rotation
			((subdata == 2) &  90) |   // 90deg anti-clock
			((subdata == 3) & 180) ) ; // 90deg clock
    }
  
  if (ver == eBiffV8)
    {
      // FIXME: Got bored and stop implementing everything, there is just too much !
      data          = BIFF_GETWORD(q->data+8) ;
      subdata       = (data&0x00C0)>>10 ;
      xf->eastern   = ( ((subdata == 0) & eBiffEContext) |
		        ((subdata == 1) & eBiffEleftToRight) |
		        ((subdata == 2) & eBiffErightToLeft) ) ;
    }
  
  if (ver == eBiffV8) // Very different now !
    {
      data          = BIFF_GETWORD(q->data+10) ;
      subdata       = data ;
      xf->border_line[eBiffDirLeft]    = (subdata&0xf) ;
      subdata = subdata>>4 ;
      xf->border_line[eBiffDirRight]   = (subdata&0xf) ;
      subdata = subdata>>4 ;
      xf->border_line[eBiffDirTop]     = (subdata&0xf) ;
      subdata = subdata>>4 ;
      xf->border_line[eBiffDirBottom]  = (subdata&0xf) ;
      subdata = subdata>>4 ;

      data          = BIFF_GETWORD(q->data+12) ;
      subdata       = data ;
      xf->border_color[eBiffDirLeft]   = (subdata&0x7f) ;
      subdata = subdata >> 7 ;
      xf->border_color[eBiffDirRight]  = (subdata&0x7f) ;
      subdata = (data&0xc000)>>30 ;
      xf->border_orientation = ( ((subdata == 1) & eBiffBODiagDown) |
				 ((subdata == 2) & eBiffBODiagUp) |
				 ((subdata == 3) & eBiffBODiagBoth) ) ;

      data          = BIFF_GETLONG(q->data+14) ;
      subdata       = data ;
      xf->border_color[eBiffDirTop]    = (subdata&0x7f) ;
      subdata = subdata >> 7 ;
      xf->border_color[eBiffDirBottom] = (subdata&0x7f) ;
      subdata = subdata >> 7 ;
      xf->border_linestyle = (data&0x01e00000)>>21 ;
      xf->fill_pattern_idx = (data&0xfc000000)>>26 ;

      data            = BIFF_GETWORD(q->data+18) ;
      xf->foregnd_col = (data&0x007f) ;
      xf->backgnd_col = (data&0x3f80)>>7 ;
    }
  else
    {
      data            = BIFF_GETWORD(q->data+8) ;
      xf->foregnd_col = (data&0x007f) ;
      xf->backgnd_col = (data&0x1f80)>>7 ;

      data                 = BIFF_GETWORD(q->data+10) ;
      xf->fill_pattern_idx = data&0x03f ;
      // Luckily this maps nicely onto the new set.
      xf->border_line[eBiffDirBottom]  = (data&0x1c0)>>6 ;
      xf->border_color[eBiffDirBottom] = (data&0xfe00)>>9 ;

      data            = BIFF_GETWORD(q->data+12) ;
      subdata         = data ;
      xf->border_line[eBiffDirTop]     = (subdata&0x07) ;
      subdata = subdata >> 3 ;
      xf->border_line[eBiffDirLeft]    = (subdata&0x07) ;
      subdata = subdata >> 3 ;
      xf->border_line[eBiffDirRight]   = (subdata&0x07) ;
      subdata = subdata >> 3 ;
      xf->border_color[eBiffDirTop]    = subdata ;

      data            = BIFF_GETWORD(q->data+14) ;
      subdata         = data ;
      xf->border_color[eBiffDirLeft]   = (subdata&0x7f) ;
      subdata = subdata >> 7 ;
      xf->border_color[eBiffDirRight]  = (subdata&0x7f) ;
    }
  return xf ;
}

static void free_biff_xf_data (BIFF_XF_DATA *d)
{
  free (d) ;
}

static MS_EXCEL_SHEET *new_ms_excel_sheet (MS_EXCEL_WORKBOOK *wb, eBiff_version ver, char *name)
{
  MS_EXCEL_SHEET *ans = (MS_EXCEL_SHEET *)malloc(sizeof(MS_EXCEL_SHEET)) ;
  ans->gnum_sheet = sheet_new (wb->gnum_wb, name) ;
  ans->wb  = wb ;
  ans->ver = ver ;
  return ans ;
}

void ms_excel_sheet_insert (MS_EXCEL_SHEET *sheet, int xfidx, int col, int row, char *text)
{
  Cell *cell ;
  if (!(   (cell = sheet_cell_get (sheet->gnum_sheet, col, row))
	 ||(cell = sheet_cell_new (sheet->gnum_sheet, col, row))))
    printf ("No cell error at [%d, %d]\n", col, row) ;
  else
    {
      cell_set_text_simple(cell, text) ;
      ms_excel_set_cell_xf(sheet, cell, xfidx) ;
    }
}

static void free_ms_excel_sheet (MS_EXCEL_SHEET *ptr)
{
  sheet_destroy (ptr->gnum_sheet) ;
  free(ptr) ;
}

static MS_EXCEL_WORKBOOK *new_ms_excel_workbook ()
{
  MS_EXCEL_WORKBOOK *ans = (MS_EXCEL_WORKBOOK *)malloc(sizeof(MS_EXCEL_WORKBOOK)) ;
  ans->gnum_wb         = NULL ;
  ans->boundsheet_data = NULL ;
  ans->excel_sheets    = NULL ;
  ans->XF_records      = NULL ;
  return ans ;
}

static void ms_excel_workbook_attach (MS_EXCEL_WORKBOOK *wb, MS_EXCEL_SHEET *ans)
{
  workbook_attach_sheet (wb->gnum_wb, ans->gnum_sheet) ;
}

static void free_ms_excel_workbook (MS_EXCEL_WORKBOOK *wb)
{
  GList *ptr = g_list_first(wb->boundsheet_data) ;
  while (ptr)
    {
      BIFF_BOUNDSHEET_DATA *dat ;
      dat = ptr->data ;
      free_biff_boundsheet_data (dat) ;
      ptr = ptr->next ;
    }
  g_list_free (wb->boundsheet_data) ;

  ptr = g_list_first(wb->XF_records) ;
  while (ptr)
    {
      BIFF_XF_DATA *dat ;
      dat = ptr->data ;
      free_biff_xf_data (dat) ;
      ptr = ptr->next ;
    }
  g_list_free (wb->XF_records) ;
}

static void ms_excel_read_cell  (BIFF_QUERY *q, MS_EXCEL_SHEET *sheet)
{
  Cell *cell ;
  
  // ----------------------------------------------------------------------------------
  //  NB. Microsoft Docs give offsets from start of biff record, subtract 4 their docs.
  // ----------------------------------------------------------------------------------
  switch (q->ls_op)
    {
    case BIFF_BLANK:      // FIXME: a cell with just XF info, but no XF yet :-)
      printf ("Cell [%d, %d] XF = %x\n", BIFF_GETCOL(q), BIFF_GETROW(q),
	      BIFF_GETXF(q)) ;
      break ;
    case BIFF_MULBLANK:   // FIXME: S95DA7.HTM - Confusing !
      printf ("Cells in row %d are blank starting at col %d until col %d\n",
	      BIFF_GETROW(q), BIFF_GETWORD(q->data+2), BIFF_GETWORD(q->data+8)) ;
      // Presumably followed by the array of XF indexes ?
      break ;
    case BIFF_RSTRING:    // Ignore formatting for now : FIXME	    
      //      printf ("Cell [%d, %d] = ", BIFF_GETCOL(q), BIFF_GETROW(q)) ;
      //	    dump (q->data, q->length) ;
      //      STRNPRINTF(q->data + 8, BIFF_GETSTRLEN(q)) ;
      ms_excel_sheet_insert (sheet, BIFF_GETXF(q), BIFF_GETCOL(q), BIFF_GETROW(q),
			     ms_get_biff_text(q->data + 8, BIFF_GETSTRLEN(q))) ;
      break;
    case BIFF_NUMBER:     // FIXME: Font info needed
      {
	char buf[65] ;
	double num = BIFF_GETDOUBLE(q->data +  6) ;
	//	      long long int l = 0x123456789abcdefLL ;
	//	printf ("Cell [%d, %d] = %f\n", BIFF_GETCOL(q), BIFF_GETROW(q), num) ;
	sprintf (buf, "%f", num) ;
	ms_excel_sheet_insert (sheet, BIFF_GETXF(q), BIFF_GETCOL(q), BIFF_GETROW(q), buf) ;
	//	      dump (q->data, q->length) ;	      
	break;
      }
    case BIFF_RK:         // FIXME: S59DDA.HTM - test IEEE stuff on other endian platforms
      {
	LONG number ;
	LONG tmp[2] ;
	char buf[65] ;
	double answer ;
	enum eType { eIEEE = 0, eIEEEx10 = 1, eInt = 2, eIntx100 = 3 } type ;

	number = BIFF_GETLONG(q->data+6) ;
	printf ("RK number : 0x%x, length 0x%x\n", q->opcode, q->length) ;
	printf ("position [%d,%d] = %lx\n", BIFF_GETCOL(q), BIFF_GETROW(q), number) ;
	// Ignore XF
	type = (number & 0x3) ;
	switch (type)
	  {
	  case eIEEE:
	    dump (q->data, q->length) ;
	    tmp[0] = number & 0xfffffffc ;
	    tmp[1] = 0 ;
	    answer = BIFF_GETDOUBLE(((BYTE *)tmp)) ;
	    break ;
	  case eIEEEx10:
	    dump (q->data, q->length) ;
	    tmp[0] = number & 0xfffffffc ;
	    tmp[1] = 0 ;
	    answer = BIFF_GETDOUBLE(((BYTE *)tmp)) ;
	    answer/=100.0 ;
	    break ;
	  case eInt:
	    answer = (double)(number>>2) ;
	    break ;
	  case eIntx100:
	    answer = ((double)(number>>2))/100.0 ;
	    break ;
	  default:
	    printf ("You don't exist go away\n") ;
	    answer = 0 ;
	  }
	sprintf (buf, "%f", answer) ;
	printf ("The answer is '%s'\n", buf) ;
	ms_excel_sheet_insert (sheet, BIFF_GETXF(q), BIFF_GETCOL(q), BIFF_GETROW(q), buf) ;
      }
      break;
    case BIFF_LABEL:      // FIXME
      ms_excel_sheet_insert (sheet, BIFF_GETXF(q), BIFF_GETCOL(q), BIFF_GETROW(q),
			     ms_get_biff_text(q->data + 8, BIFF_GETSTRLEN(q))) ;
      break;
    case BIFF_ROW:        // FIXME
      printf ("Row %d formatting\n", BIFF_GETROW(q)) ;
      break ;
    case BIFF_FORMULA:  // FIXME: S59D8F.HTM
      ms_excel_parse_formular (sheet, q) ;
      //      STRNPRINTF (q->data + 22, GETWORD(q->data+20)) ;
      break ;
    default:
      printf ("Opcode : 0x%x, length 0x%x\n", q->opcode, q->length) ;
      //      dump (q->data, q->length) ;
      break;
    }
}

static void ms_excel_read_sheet (BIFF_QUERY *q, MS_EXCEL_WORKBOOK *wb,
				 BIFF_BOUNDSHEET_DATA *bsh, eBiff_version ver)
{
  MS_EXCEL_SHEET *sheet = new_ms_excel_sheet (wb, ver, bsh->name) ;
  LONG blankSheetPos = q->streamPos + q->length + 4 ;

  while (ms_next_biff(q))
    {
      switch (q->ls_op)
	{
	case BIFF_EOF:
	  if (q->streamPos == blankSheetPos)
	    {
	      printf ("Blank sheet '%s'\n", bsh->name) ;
	      free_ms_excel_sheet (sheet) ;
	      return ;
	    }
	  else
	    ms_excel_workbook_attach (wb, sheet) ;
	  return ;
	  break ;
	default:
	  ms_excel_read_cell (q, sheet) ;
	  break ;
	}
    }
  printf ("Error, hit end without EOF\n") ;
  return ;
}

Workbook *ms_excelReadWorkbook(MS_OLE_FILE *file)
{
  MS_EXCEL_WORKBOOK *wb = NULL ;
  xmlNodePtr child ;

  if (!ms_ole_analyse_file (file))
    {
      printf ("Analysis failed\n") ;
      return 0 ;
    }
  {
    BIFF_QUERY *q ;
    BIFF_BOF_DATA *ver=0 ;

    // Tabulate frequencies for testing
    {
      int freq[256] ;
      int lp ;
      
      printf ("--------- BIFF Usage Chart ----------\n") ;
      for (lp=0;lp<256;lp++) freq[lp] = 0 ;
      q = new_ms_biff_query_file(file) ;
      while(ms_next_biff(q))
	freq[q->ls_op]++ ;
      for (lp=0;lp<256;lp++)
	if (freq[lp]>0)
	  printf ("Opcode 0x%x : %d\n", lp, freq[lp]) ;
      printf ("--------- End  Usage Chart ----------\n") ;
      free_ms_biff_query(q) ;
    }
    
    q = new_ms_biff_query_file(file) ; // Find that book file

    while (ms_next_biff(q))
      {
	switch (q->ls_op)
	  {
	  case BIFF_BOF:
	    if (ver)
	      free_ms_biff_bof_data(ver) ;
	    ver = new_ms_biff_bof_data(q) ;
	    if (ver->type == eBiffTWorkbook)
	      {
		wb = new_ms_excel_workbook () ;
		wb->gnum_wb = workbook_new () ;
	      }
	    else if (ver->type == eBiffTWorksheet)
	      {
		GList *which = g_list_first(wb->boundsheet_data) ;
		while (which)
		  {
		    BIFF_BOUNDSHEET_DATA *bsh = which->data ;
		    if (bsh->streamStartPos == q->streamPos)
		      {
			ms_excel_read_sheet (q, wb, bsh, ver->version) ;
			break ;
		      }
		    which = which->next ;
		  }
		if (!which)
		  printf ("Sheet offset in stream of %lx not found in list\n", q->streamPos) ;
	      }
	    else
	      printf ("Unknown BOF\n") ;
	    break ;
	  case BIFF_EOF:
	    printf ("End of worksheet spec.\n") ;
	    break ;
	  case BIFF_BOUNDSHEET: 
	    {
	      BIFF_BOUNDSHEET_DATA *dat = new_biff_boundsheet_data (q, ver->version) ;
	      assert (dat) ;
	      wb->boundsheet_data = g_list_append (wb->boundsheet_data, dat) ;
	    }
	    break;
	  case BIFF_PALETTE:
	    printf ("READ PALETTE\n") ;
	    wb->palette = new_ms_excel_palette (q) ;
	    break;
	  case BIFF_PRECISION: // FIXME:
	    printf ("Opcode : 0x%x, length 0x%x\n", q->opcode, q->length) ;
	    dump (q->data, q->length) ;
	    break ;
	  case BIFF_XF_OLD:    // FIXME: see S59E1E.HTM
	  case BIFF_XF:    
	    {
	      BIFF_XF_DATA *ptr = new_biff_xf_data(q, ver->version) ;
	      printf ("Extended format:\n");
	      //	      dump (q->data, q->length) ;
	      wb->XF_records = g_list_append (wb->XF_records, ptr) ;
	    }
	    break ;
	  default:
	    printf ("Opcode : 0x%x, length 0x%x\n", q->opcode, q->length) ;
	    //	    dump (q->data, q->length) ;
	    break ;	    
	  }
      }
    free_ms_biff_query(q) ;
    if (ver)
      free_ms_biff_bof_data(ver) ;
  }
  return wb->gnum_wb ;
}
