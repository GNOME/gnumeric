/*
 * ms-biff.c: MS Excel BIFF support for Gnumeric
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
#include <malloc.h>
#include <assert.h>
#include <ctype.h>

#include "ms-ole.h"
#include "ms-biff.h"

BIFF_BOF_DATA *new_ms_biff_bof_data (BIFF_QUERY *q)
{
  BIFF_BOF_DATA *ans = (BIFF_BOF_DATA *)malloc(sizeof(BIFF_BOF_DATA)) ;
  if ((q->opcode&0xff) == BIFF_BOF)
    {
      assert (q->length>=4) ;
      // Determine type from boff
      switch (q->opcode>>8)
	{
	case 0:
	  ans->version = eBiffV2 ;
	  break ;
	case 2:
	  ans->version = eBiffV3 ;
	  break ;
	case 4:
	  ans->version = eBiffV4 ;
	  break ;
	case 8: // MOre complicated
	  {
	    switch (GETWORD(q->data))
	      {
	      case 0x0600:
		ans->version = eBiffV8 ;
		break ;
	      case 0x500:
		ans->version = eBiffV5 ; // OR ebiff7 : FIXME ? !
		break ;
	      default:
		printf ("Unknown BIFF sub-number in BOF %x\n", q->opcode) ;
		ans->version = eBiffVUnknown ;
	      }
	  }
	  break;
	default:
	  printf ("Unknown BIFF number in BOF %x\n", q->opcode) ;
	  ans->version = eBiffVUnknown ;
	}
      switch (GETWORD(q->data+2))
	{
	case 0x0005:
	  ans->type = eBiffTWorkbook ;
	  break ;
	case 0x0006:
	  ans->type = eBiffTVBModule ;
	  break ;
	case 0x0010:
	  ans->type = eBiffTWorksheet ;
	  break ;
	case 0x0020:
	  ans->type = eBiffTChart ;
	  break ;
	case 0x0040:
	  ans->type = eBiffTMacrosheet ;
	  break ;
	case 0x0100:
	  ans->type = eBiffTWorkspace ;
	  break ;
	default:
	  ans->type = eBiffTUnknown ;
	  printf ("Unknown BIFF type in BOF %x\n", GETWORD(q->data+2)) ;
	  break ;
	}
      // Now store in the directory array:
      printf ("BOF %x, %d == %d, %d\n", q->opcode, q->length,
	      ans->version, ans->type) ;
    }
  else
    {
      printf ("Not a BOF !\n") ;
      ans->version = eBiffVUnknown ;
      ans->type    = eBiffTUnknown ;
    }
  return ans ;
}

void free_ms_biff_bof_data (BIFF_BOF_DATA *data)
{
  free (data) ;
}
