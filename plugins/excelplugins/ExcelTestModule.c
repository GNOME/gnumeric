/*
 * ExcelTestModule.c: Standalone XLL code for test purposes.
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
#include <gmodule.h>

#if defined( WIN32 ) || defined( WIN64 )
#include <windef.h>
#else
#include "win32replacements.h"
#endif

#include "xlcall.h"

/* All bits that are mutually exclusive in the type field of an xloper. */
#define xltypeType       0x0FFF

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define g_slice_new_array0(T,c) ((T*) g_slice_alloc0 ((sizeof (T))*(c)))
#define ALLOC_ARRAY(T,c) (g_slice_new_array0 (T,c))
#define FREE_ARRAY(p,c) (g_slice_free1 (sizeof(*p)*(c),(p)))

static char * pascal_string_from_c_string(const char *s){
	char *o=NULL;
	if (NULL!=s){
		guint l = strlen(s);
		g_assert(l<(UINT_MAX-2U));
		o = g_malloc(l+2U);
		g_strlcpy(o+1,s,l+1);
		if (l>UCHAR_MAX)
			l=UCHAR_MAX;
		o[0]=(unsigned char)l;
	}
	return o;
}

;

static char * duplicate_pascal_string(const char *s){
	return NULL==s?NULL:go_memdup(s,s[0]+1);
}

#define CASE( x ) case (x): return #x

static const char * xloper_type_name(const XLOPER*x){
	if (NULL!=x){
		switch(x->xltype & xltypeType){
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

static void unsupported_xloper_type(const XLOPER*x){
	g_warning(("Unsupported xloper type \"%s\""),xloper_type_name(x));
}

static void destruct_xloper(XLOPER*x){
	int i,n;
	if (NULL!=x){
		switch(x->xltype & xltypeType){
		case xltypeNum:							break;
		case xltypeStr:		g_free(x->val.str); x->val.str=0;	break;
		case xltypeBool:						break;
		case xltypeRef:		unsupported_xloper_type(x);		break;
		case xltypeErr:							break;
		case xltypeFlow:	unsupported_xloper_type(x);		break;
		case xltypeMulti:
			n=x->val.array.rows*x->val.array.columns;
			for (i=0;i<n;++i){
				destruct_xloper(x->val.array.lparray+i);
			}
			FREE_ARRAY(x->val.array.lparray,n);
			break;
		case xltypeMissing:						break;
		case xltypeNil:							break;
		case xltypeSRef:	unsupported_xloper_type(x);		break;
		case xltypeInt:							break;
		default:		unsupported_xloper_type(x);
		}
		x->xltype=xltypeNil;
	}
}

static void copy_construct_xloper(XLOPER*x,const XLOPER*y){
	int i,n;
	if (NULL!=x){
		x->xltype=xltypeMissing;
		if(NULL!=y){
			memmove(x,y,sizeof(XLOPER));
			switch(y->xltype & xltypeType){
			case xltypeStr:
				x->val.str=duplicate_pascal_string(y->val.str);
				break;
			case xltypeMulti:
				n=y->val.array.rows*y->val.array.columns;
				x->val.array.lparray=ALLOC_ARRAY(XLOPER,n);
				for (i=0;i<n;++i){
					copy_construct_xloper(x->val.array.lparray+i,y->val.array.lparray+i);
				}
				break;
			default:;
			}
			x->xltype = (y->xltype & xltypeType);
		}

	}
}

G_MODULE_EXPORT BOOL APIENTRY DllMain( HANDLE hDLL, DWORD dwReason, LPVOID lpReserved ) {
  return TRUE;
}

G_MODULE_EXPORT const XLOPER* hello(){
	XLOPER*r=ALLOC_ARRAY(XLOPER,1);
	r->xltype=xltypeStr|xlbitDLLFree;
	r->val.str=pascal_string_from_c_string("Hello!");
	return r;
}

G_MODULE_EXPORT const XLOPER* helloWorld(){
	XLOPER*r=ALLOC_ARRAY(XLOPER,1);
	r->xltype=xltypeStr|xlbitDLLFree;
	r->val.str=pascal_string_from_c_string("Hello World!");
	return r;
}

G_MODULE_EXPORT const XLOPER* convertAstronomicalUnitsToKilometers(const XLOPER*x){
	XLOPER*r=NULL;
	if ( NULL!=x && (x->xltype&xltypeType)==xltypeNum ) {
		r=ALLOC_ARRAY(XLOPER,1);
		r->xltype=xltypeNum|xlbitDLLFree;
		r->val.num=x->val.num*149600000.;
	}
	return r;
}

G_MODULE_EXPORT const XLOPER* multiplyTwoNumbers(const XLOPER*x,const XLOPER*y){
	XLOPER*r=NULL;
	if ( NULL!=x && (x->xltype&xltypeType)==xltypeNum && NULL!=y && (y->xltype&xltypeType)==xltypeNum ) {
		r=ALLOC_ARRAY(XLOPER,1);
		r->xltype=xltypeNum|xlbitDLLFree;
		r->val.num=x->val.num*y->val.num;
	}
	return r;
}

G_MODULE_EXPORT const XLOPER* arrangeInSquareMatrix(const XLOPER*a,const XLOPER*b,const XLOPER*c,const XLOPER*d){
	XLOPER*r=NULL;
	if ( NULL!=a && NULL!=b && NULL!=c && NULL!=d ) {
		r=ALLOC_ARRAY(XLOPER,1);
		r->xltype=xltypeMulti|xlbitDLLFree;
		r->val.array.rows=2;
		r->val.array.columns=2;
		r->val.array.lparray=ALLOC_ARRAY(XLOPER,4);
		copy_construct_xloper(r->val.array.lparray  ,a);
		copy_construct_xloper(r->val.array.lparray+1,b);
		copy_construct_xloper(r->val.array.lparray+2,c);
		copy_construct_xloper(r->val.array.lparray+3,d);

	}
	return r;
}

G_MODULE_EXPORT const XLOPER* sumAndProduct(const XLOPER*m){
	XLOPER*r=NULL;
	if ( NULL!=m ) {
		r=ALLOC_ARRAY(XLOPER,1);
		r->xltype=xltypeMulti|xlbitDLLFree;
		r->val.array.rows=1;
		r->val.array.columns=2;
		r->val.array.lparray=ALLOC_ARRAY(XLOPER,2);
		r->val.array.lparray[0].xltype=xltypeNum;
		r->val.array.lparray[0].val.num = 0;
		r->val.array.lparray[1].xltype=xltypeNum;
		r->val.array.lparray[1].val.num = 1;
		if ((m->xltype&xltypeType)==xltypeMulti){
			int i,j;
			for (i=0;i<m->val.array.columns;++i){
				for (j=0;j<m->val.array.rows;++j){
					const double x = m->val.array.lparray[i*m->val.array.rows+j].val.num;
					r->val.array.lparray[0].val.num += x;
					r->val.array.lparray[1].val.num *= x;
				}
			}
		}

	}
	return r;
}

typedef const char * Excel4RegistrationInfo[30];

static Excel4RegistrationInfo registration_info[] = {
  {
    "helloWorld", "P", "helloWorld", "", "1", "Friendly", "", "", "helloWorld() returns \"Hello World!\".",
    NULL
  },
  {
    "hello", "P", "hello",      "", "1", "Friendly", "", "", "hello() returns \"Hello\".",
    NULL
  },
  {
    "convertAstronomicalUnitsToKilometers", "PP", "convertAstronomicalUnitsToKilometers", "au", "1", "Astronomical", "", "", "convertAstronomicalUnitsToKilometers(au) returns au * 149600000.",
    "astronomical units", NULL
  },
  {
    "multiplyTwoNumbers", "PPP", "multiplyTwoNumbers", "x,y", "1", "Mathematics", "", "", "multiplyTwoNumbers(X,Y) returns X*Y.",
    "first multiplicand", "second multiplicand", NULL
  },
  {
    "arrangeInSquareMatrix", "PPPPP", "arrangeInSquareMatrix", "a,b,c,d", "1", "Mathematics", "", "", "arrangeInSquareMatrix(a,b,c,d) returns a square matrix {{a,b},{c,d}}.",
    "top left entry", "top right entry", "bottom left entry", "bottom right entry", NULL
  },
  {
    "sumAndProduct", "PP", "sumAndProduct", "M", "1", "Mathematics", "", "", "sumAndProduct(M) returns the sum and the product of the matrix M.",
    "a matrix", NULL
  }
};

static void registerAllFunctions(void){
  XLOPER xlopers[30];
  XLOPER *excel4vArgs[30];
  XLOPER xlRet;
  unsigned long i,j;
  const unsigned long n = sizeof(registration_info)/sizeof(Excel4RegistrationInfo);
  for (i=0;i<30;++i){
    excel4vArgs[i]=xlopers+i;
  }
  Excel4(xlGetName, excel4vArgs[0], 0);
  for (i=0;i<n;++i){
    for (j=0;j<29&&NULL!=registration_info[i][j];++j){
      excel4vArgs[1+j]->xltype=xltypeStr;
      excel4vArgs[1+j]->val.str=pascal_string_from_c_string(registration_info[i][j]);
    }
    Excel4v(xlfRegister, &xlRet, j+1, excel4vArgs);
    for (j=0;j<29&&NULL!=registration_info[i][j];++j){
      g_free(excel4vArgs[1+j]->val.str);
    }
  }
}

G_MODULE_EXPORT int xlAutoOpen() {
  registerAllFunctions();
  return 1;
}

G_MODULE_EXPORT int xlAutoClose() {
  return 1;
}

G_MODULE_EXPORT void xlAutoFree(XLOPER*p) {
  destruct_xloper(p);
  FREE_ARRAY(p,1);
  return;
}
