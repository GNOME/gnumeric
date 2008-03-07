/* glplib2.c */

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

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "glplib.h"

/*----------------------------------------------------------------------
-- lib_init_env - initialize library environment.
--
-- *Synopsis*
--
-- #include "glplib.h"
-- int lib_init_env(void);
--
-- *Description*
--
-- The routine lib_init_env initializes the library environment block
-- used by other low-level library routines.
--
-- This routine is called automatically on the first call some library
-- routine, so the user needn't to call this routine explicitly.
--
-- *Returns*
--
-- The routine lib_init_env returns one of the following codes:
--
-- 0 - no errors;
-- 1 - the library environment has been already initialized;
-- 2 - initialization failed (insufficient memory). */

int lib_init_env(void)
{     LIBENV *env;
      int k;
      /* retrieve a pointer to the environmental block */
      env = lib_get_ptr();
      /* check if the environment has been already initialized */
      if (env != NULL) return 1;
      /* allocate the environmental block */
      env =g_malloc (sizeof(LIBENV));
      /* check if the block has been successfully allocated */
      if (env == NULL) return 2;
      /* store a pointer to the environmental block */
      lib_set_ptr(env);
      /* initialize the environmental block */
      env->print_info = NULL;
      env->print_hook = NULL;
      env->fault_info = NULL;
      env->fault_hook = NULL;
      env->mem_ptr = NULL;
      env->mem_limit = INT_MAX;
      env->mem_total = 0;
      env->mem_tpeak = 0;
      env->mem_count = 0;
      env->mem_cpeak = 0;
      for (k = 0; k < LIB_MAX_OPEN; k++) env->file_slot[k] = NULL;
#if 1 /* 14/I-2006 */
      env->hcpy_file = NULL;
#endif
      /* initialization completed */
      return 0;
}

/*----------------------------------------------------------------------
-- lib_env_ptr - retrieve a pointer to the environmental block.
--
-- *Synopsis*
--
-- #include "glplib.h"
-- LIBENV *lib_env_ptr(void);
--
-- *Description*
--
-- The routine lib_env_ptr retrieves and returns a pointer to the
-- library environmental block.
--
-- If the library environment has not been initialized yet, the routine
-- performs initialization. If initialization fails, the routine prints
-- an error message to stderr and terminates the program.
--
-- *Returns*
--
-- The routine returns a pointer to the library environmental block. */

LIBENV *lib_env_ptr(void)
{     LIBENV *env;
      /* retrieve a pointer to the environmental block */
      env = lib_get_ptr();
      /* check if the environment has been already initialized */
      if (env == NULL)
      {  /* not initialized yet; perform initialization */
         if (lib_init_env() != 0)
         {  /* initialization failed; print an error message */
            fprintf(stderr, "\n");
            fprintf(stderr, "lib_env_ptr: library environment initializ"
               "ation failed\n");
            fflush(stderr);
            /* and abnormally terminate the program */
            exit(EXIT_FAILURE);
         }
         /* initialization completed; retrieve a pointer */
         env = lib_get_ptr();
      }
      return env;
}

/*----------------------------------------------------------------------
-- lib_free_env - free library environment.
--
-- *Synopsis*
--
-- #include "glplib.h"
-- int lib_free_env(void);
--
-- *Description*
--
-- The routine lib_free_env frees all resources (memory blocks, etc.),
-- which was allocated by the library routines and which are currently
-- still in use.
--
-- The user needn't to call this routine until he wishes to explicitly
-- free all the resources.
--
-- *Returns*
--
-- 0 - no errors;
-- 1 - the library environment is inactive (not initialized). */

int lib_free_env(void)
{     LIBENV *env;
      int k;
      /* retrieve a pointer to the environmental block */
      env = lib_get_ptr();
      /* check if the environment is active */
      if (env == NULL) return 1;
      /* free memory blocks, which are still allocated */
      while (env->mem_ptr != NULL)
      {  LIBMEM *blk = env->mem_ptr;
         env->mem_ptr = blk->next;
        g_free (blk);
      }
      /* close i/o streams, which are still open */
      for (k = 0; k < LIB_MAX_OPEN; k++)
         if (env->file_slot[k] != NULL) fclose(env->file_slot[k]);
      /* free memory allocated to the environmental block */
     g_free (env);
      /* reset a pointer to the environmental block */
      lib_set_ptr(NULL);
      /* deinitialization completed */
      return 0;
}

#if 1 /* 14/I-2006 */
int lib_open_hardcopy(char *filename)
{     /* open hardcopy file */
      LIBENV *env = lib_env_ptr();
      if (env->hcpy_file != NULL)
      {  /* hardcopy file is already open */
         return 1;
      }
      env->hcpy_file = ufopen(filename, "w");
      if (env->hcpy_file == NULL)
      {  /* cannot create hardcopy file */
         return 2;
      }
      setvbuf(env->hcpy_file, NULL, _IOLBF, BUFSIZ);
      return 0;
}

int lib_close_hardcopy(void)
{     /* close hardcopy file */
      LIBENV *env = lib_env_ptr();
      if (env->hcpy_file == NULL)
      {  /* hardcopy file is already closed */
         return 1;
      }
      ufclose(env->hcpy_file);
      env->hcpy_file = NULL;
      return 0;
}
#endif

/*----------------------------------------------------------------------
-- print - print informative message.
--
-- *Synopsis*
--
-- #include "glplib.h"
-- void print(char *fmt, ...);
--
-- *Description*
--
-- The routine print prints an informative message specified by the
-- format control string fmt and the optional parameter list. */

void print(const char *fmt, ...)
{     LIBENV *env = lib_env_ptr();
      va_list arg;
      char msg[4095+1];
      /* format the message */
      va_start(arg, fmt);
      vsprintf(msg, fmt, arg);
      insist(strlen(msg) <= 4095);
      va_end(arg);
      /* pass the message to the user-defined hook routine */
      if (env->print_hook != NULL &&
          env->print_hook(env->print_info, msg) != 0) goto skip;
      /* send the message to the standard output */
      fprintf(stdout, "%s\n", msg);
#if 1 /* 14/I-2006 */
      if (env->hcpy_file != NULL)
         fprintf(env->hcpy_file, "%s\n", msg);
#endif
skip: /* return to the calling program */
      return;
}

/*----------------------------------------------------------------------
-- lib_set_print_hook - install print hook routine.
--
-- *Synopsis*
--
-- #include "glplib.h"
-- void lib_set_print_hook(void *info, int (*hook)(void *info,
--    char *msg));
--
-- *Description*
--
-- The routine lib_set_print_hook installs the user-defined print hook
-- routine.
--
-- The parameter info is a transit pointer passed to the hook routine.
--
-- The parameter hook is an entry point to the user-defined print hook
-- routine. This routine is called by the routine print every time when
-- an informative message should be output. The routine print passes to
-- the hook routine the transit pointer info and the character string
-- msg, which contains the message. If the hook routine returns zero,
-- the routine print prints the message in an usual way. Otherwise, if
-- the hook routine returns non-zero, the message is not printed.
--
-- In order to uninstall the hook routine the parameter hook should be
-- specified as NULL (in this case the parameter info is ignored). */

void lib_set_print_hook(void *info, int (*hook)(void *info, const char *msg))
{     LIBENV *env = lib_env_ptr();
      env->print_info = info;
      env->print_hook = hook;
      return;
}

/*----------------------------------------------------------------------
-- fault - print error message and terminate program execution.
--
-- *Synopsis*
--
-- #include "glplib.h"
-- void fault(char *fmt, ...);
--
-- *Description*
--
-- The routine fault prints an error message specified by the format
-- control string fmt and the optional parameter list, then terminates
-- execution of the program. */

void fault(const char *fmt, ...)
{     LIBENV *env = lib_env_ptr();
      va_list arg;
      char msg[4095+1];
      /* format the message */
      va_start(arg, fmt);
      vsprintf(msg, fmt, arg);
      insist(strlen(msg) <= 4095);
      va_end(arg);
      /* pass the message to the user-defined hook routine */
      if (env->fault_hook != NULL &&
          env->fault_hook(env->fault_info, msg) != 0) goto skip;
      /* send the message to the standard output */
      fprintf(stdout, "%s\n", msg);
#if 1 /* 14/I-2006 */
      if (env->hcpy_file != NULL)
         fprintf(env->hcpy_file, "%s\n", msg);
#endif
skip: /* terminate program execution */
      exit(EXIT_FAILURE);
      /* no return */
}

/*----------------------------------------------------------------------
-- lib_set_fault_hook - install fault hook routine.
--
-- *Synopsis*
--
-- #include "glplib.h"
-- void lib_set_fault_hook(void *info, int (*hook)(void *info,
--    char *msg));
--
-- *Description*
--
-- The routine lib_set_fault_hook installs the user-defined fault hook
-- routine.
--
-- The parameter info is a transit pointer passed to the hook routine.
--
-- The parameter hook is an entry point to the user-defined fault hook
-- routine. This routine is called by the routine fault when an error
-- message should be printed. The routine fault passes to the hook
-- routine the transit pointer info and the character string msg, which
-- contains the message. If the hook routine returns zero, the routine
-- fault prints the message in an usual way. Otherwise, if the hook
-- routine returns non-zero, the message is not printed.
--
-- After returning from the hook routine, the routine fault terminates
-- program execution. If it's necessary to prevent program termination,
-- the hook routine should not return via the return statement; instead
-- that it should perform global go to via longjmp. Note that if in the
-- latter case the library environment is planned to be re-initialized,
-- it at first should be freed by using the routine lib_free_env.
--
-- In order to uninstall the hook routine the parameter hook should be
-- specified as NULL (in this case the parameter info is ignored). */

void lib_set_fault_hook(void *info, int (*hook)(void *info, const char *msg))
{     LIBENV *env = lib_env_ptr();
      env->fault_info = info;
      env->fault_hook = hook;
      return;
}

/*----------------------------------------------------------------------
-- insist - check for logical condition.
--
-- *Synopsis*
--
-- #include "glplib.h"
-- void insist(int expr);
--
-- *Description*
--
-- The routine insist (implemented as a macro) checks for a logical
-- condition specified by the parameter expr. If the condition is false
-- (i.e. expr is zero), the routine prints an appropriate error message
-- and abnormally terminates the program.
--
-- This routine is a replacement of the standard function assert. */

void _insist(const char *expr, const char *file, int line)
{     fault("Assertion failed: %s; file %s; line %d", expr, file, line);
      /* no return */
}

/*----------------------------------------------------------------------
-- umalloc - allocate memory block.
--
-- *Synopsis*
--
-- #include "glpset.h"
-- void *umalloc(int size);
--
-- *Description*
--
-- The routine umalloc allocates a memory block of size bytes long.
--
-- Note that being allocated the memory block contains arbitrary data
-- (not binary zeros).
--
-- *Returns*
--
-- The routine umalloc returns a pointer to the allocated memory block.
-- To free this block the routine ufree (not free!) should be used. */

#ifndef _GLPLIB_HUGEMEM
void *umalloc(int size)
{     LIBENV *env = lib_env_ptr();
      LIBMEM *desc;
      int size_of_desc = align_datasize(sizeof(LIBMEM));
      if (size < 1)
         fault("umalloc: size = %d; invalid parameter", size);
      if (size > INT_MAX - size_of_desc)
         fault("umalloc: size = %d; size too big", size);
      size += size_of_desc;
      if (size > env->mem_limit - env->mem_total)
         fault("umalloc: size = %d; no memory available", size);
      desc =g_malloc (size);
      if (desc == NULL)
         fault("umalloc: size = %d; malloc failed", size);
#if 1
      memset(desc, '?', size);
#endif
      desc->size = size;
      desc->flag = LIB_MEM_FLAG;
      desc->prev = NULL;
      desc->next = env->mem_ptr;
      if (desc->next != NULL) desc->next->prev = desc;
      env->mem_ptr = desc;
      env->mem_total += size;
      if (env->mem_tpeak < env->mem_total)
         env->mem_tpeak = env->mem_total;
      env->mem_count++;
      if (env->mem_cpeak < env->mem_count)
         env->mem_cpeak = env->mem_count;
      return (void *)((char *)desc + size_of_desc);
}
#else
void *umalloc(int size)
{     void *ptr;
      ptr =g_malloc (size);
      if (ptr == NULL)
         fault("umalloc: size = %d; malloc failed", size);
      return ptr;
}
#endif

/*----------------------------------------------------------------------
-- ucalloc - allocate memory block.
--
-- *Synopsis*
--
-- #include "glpset.h"
-- void *ucalloc(int nmemb, int size);
--
-- *Description*
--
-- The routine ucalloc allocates a memory block of (nmemb*size) bytes
-- long.
--
-- Note that being allocated the memory block contains arbitrary data
-- (not binary zeros).
--
-- *Returns*
--
-- The routine ucalloc returns a pointer to the allocated memory block.
-- To free this block the routine ufree (not free!) should be used. */

#ifndef _GLPLIB_HUGEMEM
void *ucalloc(int nmemb, int size)
{     if (nmemb < 1)
         fault("ucalloc: nmemb = %d; invalid parameter", nmemb);
      if (size < 1)
         fault("ucalloc: size = %d; invalid parameter", size);
      if (nmemb > INT_MAX / size)
         fault("ucalloc: nmemb = %d; size = %d; array too big",
            nmemb, size);
      return umalloc(nmemb * size);
}
#else
void *ucalloc(int nmemb, int size)
{     void *ptr;
      ptr = calloc(nmemb, size);
      if (ptr == NULL)
         fault("ucalloc: nmemb = %d; size = %d; calloc failed", nmemb,
            size);
      return ptr;
}
#endif

/*----------------------------------------------------------------------
-- ufree - free memory block.
--
-- *Synopsis*
--
-- #include "glpset.h"
-- void ufree(void *ptr);
--
-- *Description*
--
-- The routine ufree frees the memory block pointed to by ptr and which
-- was previuosly allocated by the routine umalloc or ucalloc. */

#ifndef _GLPLIB_HUGEMEM
void ufree(void *ptr)
{     LIBENV *env = lib_env_ptr();
      LIBMEM *desc;
      int size_of_desc = align_datasize(sizeof(LIBMEM));
      if (ptr == NULL)
         fault("ufree: ptr = %p; null pointer", ptr);
      desc = (void *)((char *)ptr - size_of_desc);
      if (desc->flag != LIB_MEM_FLAG)
         fault("ufree: ptr = %p; invalid pointer", ptr);
      if (env->mem_total < desc->size || env->mem_count == 0)
         fault("ufree: ptr = %p; memory allocation error", ptr);
      if (desc->prev == NULL)
         env->mem_ptr = desc->next;
      else
         desc->prev->next = desc->next;
      if (desc->next == NULL)
         ;
      else
         desc->next->prev = desc->prev;
      env->mem_total -= desc->size;
      env->mem_count--;
      memset(desc, '?', size_of_desc);
     g_free (desc);
      return;
}
#else
void ufree(void *ptr)
{    g_free (ptr);
      return;
}
#endif

/*----------------------------------------------------------------------
-- ufopen - open file.
--
-- *Synopsis*
--
-- #include "glplib.h"
-- void *ufopen(char *fname, char *mode);
--
-- *Description*
--
-- The routine ufopen opens a file using the character string fname as
-- the file name and the character string mode as the open mode.
--
-- *Returns*
--
-- If the file has been open successfully, the routine ufopen returns a
-- pointer to an i/o stream associated with the file (i.e. a pointer to
-- an object of FILE type). Otherwise the routine return NULL. */

void *ufopen(char *fname, char *mode)
{     LIBENV *env = lib_env_ptr();
      int k;
      /* find free slot */
      for (k = 0; k < LIB_MAX_OPEN; k++)
         if (env->file_slot[k] == NULL) break;
      if (k == LIB_MAX_OPEN)
         fault("ufopen: too many open files");
      /* open a file and store a pointer to the i/o stream */
      env->file_slot[k] = fopen(fname, mode);
      return env->file_slot[k];
}

/*----------------------------------------------------------------------
-- ufclose - close file.
--
-- *Synopsis*
--
-- #include "glplib.h"
-- void ufclose(void *fp);
--
-- *Description*
--
-- The routine ufclose closes a file associated with i/o stream, which
-- the parameter fp points to. It is assumed that the file was open by
-- the routine ufopen. */

void ufclose(void *fp)
{     LIBENV *env = lib_env_ptr();
      int k;
      /* check if the i/o stream pointer is valid */
      if (fp == NULL)
         fault("ufclose: fp = %p; null i/o stream", fp);
      for (k = 0; k < LIB_MAX_OPEN; k++)
         if (env->file_slot[k] == fp) break;
      if (k == LIB_MAX_OPEN)
         fault("ufclose: fp = %p; invalid i/o stream", fp);
      /* close a file and free the corresponding slot */
      fclose(fp);
      env->file_slot[k] = NULL;
      return;
}

/* eof */
