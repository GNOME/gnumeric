/* glplib.h */

/*----------------------------------------------------------------------
-- Copyright (C) 2000, 2001, 2002 Andrew Makhorin <mao@mai2.rcnet.ru>,
--               Department for Applied Informatics, Moscow Aviation
--               Institute, Moscow, Russia. All rights reserved.
--
-- This file is a part of GLPK (GNU Linear Programming Kit).
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
-- Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
-- 02111-1307, USA.
----------------------------------------------------------------------*/

#ifndef _GLPLIB_H
#define _GLPLIB_H

#include "gnumeric-config.h"
#include "gnumeric.h"
#include "numbers.h"

#define lib_set_ptr           glp_lib_set_ptr
#define lib_get_ptr           glp_lib_get_ptr

#define lib_init_env          glp_lib_init_env
#define lib_env_ptr           glp_lib_env_ptr
#define lib_free_env          glp_lib_free_env
#define print                 glp_lib_print
#define lib_set_print_hook    glp_lib_set_print_hook
#define fault                 glp_lib_fault
#define lib_set_fault_hook    glp_lib_set_fault_hook
#define _insist               glp_lib_insist
#define umalloc               glp_lib_umalloc
#define ucalloc               glp_lib_ucalloc
#define ufree                 glp_lib_ufree
#define utime                 glp_lib_utime

typedef struct LIBENV LIBENV;
typedef struct LIBMEM LIBMEM;

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
      /* dynamic memory management */
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

#define LIBMEM_FLAG 0x20101960
/* value used as memory block descriptor flag */

void lib_set_ptr(void *ptr);
/* store a pointer */

void *lib_get_ptr(void);
/* retrieve a pointer */

int lib_init_env(void);
/* initialize library environment */

LIBENV *lib_env_ptr(void);
/* retrieve a pointer to the environmental block */

int lib_free_env(void);
/* free library environment */

void print(char *fmt, ...);
/* print informative message */

void lib_set_print_hook(void *info, int (*hook)(void *info, char *msg));
/* install print hook routine */

void fault(char *fmt, ...);
/* print error message and terminate program execution */

void lib_set_fault_hook(void *info, int (*hook)(void *info, char *msg));
/* install fault hook routine */

#define insist(expr) \
((void)((expr) || (_insist(#expr, __FILE__, __LINE__), 1)))

void _insist(char *expr, char *file, int line);
/* check for logical condition */

/* some processors need data to be properly aligned; the align_boundary
   macro defines the boundary, which should fit for all data types; the
   align_datasize macro allows enlarging size of data item in order the
   immediately following data of any type should be properly aligned */

#define align_boundary sizeof(gnum_float)

#define align_datasize(size) \
((((size) + (align_boundary - 1)) / align_boundary) * align_boundary)

void *umalloc(int size);
/* allocate memory block */

void *ucalloc(int nmemb, int size);
/* allocate memory block */

void ufree(void *ptr);
/* free memory block */

gnum_float utime(void);
/* determine the current universal time */

/**********************************************************************/

#define create_pool           glp_create_pool
#define get_atom              glp_get_atom
#define free_atom             glp_free_atom
#define get_atomv             glp_get_atomv
#define clear_pool            glp_clear_pool
#define delete_pool           glp_delete_pool

typedef struct POOL POOL;

struct POOL
{     /* memory pool (a set of atoms) */
      int size;
      /* size of each atom in bytes (1 <= size <= 256); if size = 0,
         different atoms may have different sizes */
      void *avail;
      /* pointer to the linked list of free atoms */
      void *link;
      /* pointer to the linked list of allocated blocks (it points to
         the last recently allocated block) */
      int used;
      /* number of bytes used in the last allocated block */
      void *stock;
      /* pointer to the linked list of free blocks */
      int count;
      /* total number of allocated atoms */
};

POOL *create_pool(int size);
/* create memory pool */

void *get_atom(POOL *pool);
/* allocate atom of fixed size */

void free_atom(POOL *pool, void *ptr);
/* free an atom */

void *get_atomv(POOL *pool, int size);
/* allocate atom of variable size */

void clear_pool(POOL *pool);
/* free all atoms */

void delete_pool(POOL *pool);
/* delete memory pool */

#endif

/* eof */
