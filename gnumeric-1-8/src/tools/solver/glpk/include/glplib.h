/* glplib.h (miscellaneous low-level routines) */

/*----------------------------------------------------------------------
-- This code is part of GNU Linear Programming Kit (GLPK).
--
-- Copyright (C) 2000, 01, 02, 03, 04, 05, 06 Andrew Makhorin,
-- Department for Applied Informatics, Moscow Aviation Institute,
-- Moscow, Russia. All rights reserved. E-mail: <mao@mai2.rcnet.ru>.
--
-- GLPK is free software; you can redistribute it and/or modify it
-- under the terms of the GNU General Public License as published by
-- the Free Software Foundation; either version 2, or (at your option)
-- any later version.
--
-- GLPK is distributed in the hope that it will be useful, but WITHOUT
-- ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
-- or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public
-- License for more details.
--
-- You should have received a copy of the GNU General Public License
-- along with GLPK; see the file COPYING. If not, write to the Free
-- Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
-- 02110-1301, USA.
----------------------------------------------------------------------*/

#ifndef _GLPLIB_H
#define _GLPLIB_H

#include "gnumeric-config.h"
#include "gnumeric.h"
#include "numbers.h"

#define lib_set_ptr           glp_lib_set_ptr
#define lib_get_ptr           glp_lib_get_ptr
#define lib_get_time          glp_lib_get_time
#define lib_init_env          glp_lib_init_env
#define lib_env_ptr           glp_lib_env_ptr
#define lib_free_env          glp_lib_free_env
#define lib_open_hardcopy     glp_lib_open_hardcopy
#define lib_close_hardcopy    glp_lib_close_hardcopy
#define print                 glp_lib_print
#define lib_set_print_hook    glp_lib_set_print_hook
#define fault                 glp_lib_fault
#define lib_set_fault_hook    glp_lib_set_fault_hook
#define _insist               glp_lib_insist
#define umalloc               glp_lib_umalloc
#define ucalloc               glp_lib_ucalloc
#define ufree                 glp_lib_ufree
#define ufopen                glp_lib_ufopen
#define ufclose               glp_lib_ufclose
#define str2int               glp_lib_str2int
#define str2dbl               glp_lib_str2dbl
#define strspx                glp_lib_strspx
#define strtrim               glp_lib_strtrim
#define fp2rat                glp_lib_fp2rat
#define write_bmp16           glp_lib_write_bmp16

typedef struct LIBENV LIBENV;
typedef struct LIBMEM LIBMEM;

#define LIB_MAX_OPEN 20
/* maximal number of simultaneously open i/o streams */

struct LIBENV
{     /* library environmental block */
      /*--------------------------------------------------------------*/
      /* user-defined hook routines */
      void *print_info;
      /* transit pointer passed to the routine print_hook */
      int (*print_hook)(void *info, char *msg);
      /* user-defined print hook routine */
      void *fault_info;
      /* transit pointer passed to the routine fault_hook */
      int (*fault_hook)(void *info, char *msg);
      /* user-defined fault hook routine */
      /*--------------------------------------------------------------*/
      /* dynamic memory registration */
      LIBMEM *mem_ptr;
      /* pointer to the linked list of allocated memory blocks */
      int mem_limit;
      /* maximal amount of memory (in bytes) available for dynamic
         allocation */
      int mem_total;
      /* total amount of currently allocated memory (in bytes; is the
         sum of the size fields over all memory block descriptors) */
      int mem_tpeak;
      /* peak value of mem_total */
      int mem_count;
      /* total number of currently allocated memory blocks */
      int mem_cpeak;
      /* peak value of mem_count */
      /*--------------------------------------------------------------*/
      /* input/output streams registration */
      void *file_slot[LIB_MAX_OPEN]; /* FILE *file_slot[]; */
      /* file_slot[k], 0 <= k <= LIB_MAX_OPEN-1, is a pointer to k-th
         i/o stream; if k-th slot is free, file_slot[k] is NULL */
#if 1 /* 14/I-2006 */
      void *hcpy_file; /* FILE *hcpy_file; */
      /* pointer to output stream used for hardcopying messages output
         on the screen by the print and fault routines; NULL means the
         hardcopy file is not used */
#endif
};

struct LIBMEM
{     /* memory block descriptor */
      int size;
      /* size of block (in bytes, including descriptor) */
      int flag;
      /* descriptor flag */
      LIBMEM *prev;
      /* pointer to the previous memory block descriptor */
      LIBMEM *next;
      /* pointer to the next memory block descriptor */
      /* actual data start here (there may be a "hole" between the next
         field and actual data due to data alignment) */
};

#define LIB_MEM_FLAG 0x20101960
/* value used as memory block descriptor flag */

void lib_set_ptr(void *ptr);
/* store a pointer */

void *lib_get_ptr(void);
/* retrieve a pointer */

gnm_float lib_get_time(void);
/* determine the current universal time */

int lib_init_env(void);
/* initialize library environment */

LIBENV *lib_env_ptr(void);
/* retrieve a pointer to the environmental block */

int lib_free_env(void);
/* free library environment */

int lib_open_hardcopy(char *filename);
/* open hardcopy file */

int lib_close_hardcopy(void);
/* close hardcopy file */

void print(const char *fmt, ...);
/* print informative message */

void lib_set_print_hook(void *info, int (*hook)(void *info, const char *msg));
/* install print hook routine */

void fault(const char *fmt, ...);
/* print error message and terminate program execution */

void lib_set_fault_hook(void *info, int (*hook)(void *info, const char *msg));
/* install fault hook routine */

#define insist(expr) \
((void)((expr) || (_insist(#expr, __FILE__, __LINE__), 1)))

void _insist(const char *expr, const char *file, int line);
/* check for logical condition */

/* some processors need data to be properly aligned; the align_boundary
   macro defines the boundary, which should fit for all data types; the
   align_datasize macro allows enlarging size of data item in order the
   immediately following data of any type should be properly aligned */

#define align_boundary sizeof(gnm_float)

#define align_datasize(size) \
((((size) + (align_boundary - 1)) / align_boundary) * align_boundary)

void *umalloc(int size);
/* allocate memory block */

void *ucalloc(int nmemb, int size);
/* allocate memory block */

void ufree(void *ptr);
/* free memory block */

void *ufopen(char *fname, char *mode);
/* open file */

void ufclose(void *fp);
/* close file */

#define utime lib_get_time
/* determine the current universal time */

int str2int(char *str, int *val);
/* convert character string to value of integer type */

int str2dbl(char *str, gnm_float *val);
/* convert character string to value of gnm_float type */

char *strspx(char *str);
/* remove all spaces from character string */

char *strtrim(char *str);
/* remove trailing spaces from character string */

int fp2rat(gnm_float x, gnm_float eps, gnm_float *p, gnm_float *q);
/* convert floating-point number to rational number */

int write_bmp16(char *fname, int m, int n, char map[]);
/* write 16-color raster image in Windows Bitmap format */

#endif

/* eof */
