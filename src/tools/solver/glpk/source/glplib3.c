/* glplib3.c */

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

#include <string.h>
#include "glplib.h"

#define POOL_SIZE 8000
/* the size of each memory block for all pools (may be increased if
   necessary) */

/*----------------------------------------------------------------------
-- create_pool - create memory pool.
--
-- *Synopsis*
--
-- #include "glpset.h"
-- POOL *create_pool(int size);
--
-- *Description*
--
-- The routine create_pool creates a memory pool that is empty (i.e.
-- that contains no atoms). If size > 0 then size of each atom is set
-- to size bytes (size should be not greater than 256). Otherwise, if
-- size = 0, different atoms may have different sizes.
--
-- *Returns*
--
-- The routine create_pool returns a pointer to the created pool. */

POOL *create_pool(int size)
{     POOL *pool;
      if (!(0 <= size && size <= 256))
         fault("create_pool: invalid atom size");
      pool = umalloc(sizeof(POOL));
      /* actual atom size should be not less than sizeof(void *) and
         should be properly aligned */
      if (size > 0)
      {  if (size < sizeof(void *)) size = sizeof(void *);
         size = align_datasize(size);
      }
      pool->size = size;
      pool->avail = NULL;
      pool->link = NULL;
      pool->used = 0;
      pool->stock = NULL;
      pool->count = 0;
      return pool;
}

/*----------------------------------------------------------------------
-- get_atom - allocate atom of fixed size.
--
-- *Synopsis*
--
-- #include "glpset.h"
-- void *get_atom(POOL *pool);
--
-- *Description*
--
-- The routine get_atom allocates an atom (i.e. continuous space of
-- memory) using the specified memory pool. The size of atom which
-- should be allocated is assumed to be set when the pool was created
-- by the routine create_pool.
--
-- Note that being allocated the atom initially contains arbitrary data
-- (not binary zeros).
--
-- *Returns*
--
-- The routine get_atom returns a pointer to the allocated atom. */

void *get_atom(POOL *pool)
{     void *ptr;
      if (pool->size == 0)
         fault("get_atom: pool cannot be used to allocate an atom of fi"
            "xed size");
      /* if there is a free atom, pull it from the list */
      if (pool->avail != NULL)
      {  ptr = pool->avail;
         pool->avail = *(void **)ptr;
         goto done;
      }
      /* free atom list is empty; if the last allocated block does not
         exist or if it has not enough space, we need a new block */
      if (pool->link == NULL || pool->used + pool->size > POOL_SIZE)
      {  /* we can pull a new block from the list of free blocks, or if
            this list is empty, we need to allocate such block */
         if (pool->stock != NULL)
         {  ptr = pool->stock;
            pool->stock = *(void **)ptr;
         }
         else
            ptr = umalloc(POOL_SIZE);
         /* the new block becomes the last allocated block */
         *(void **)ptr = pool->link;
         pool->link = ptr;
         /* now only few bytes in the new block are used to hold a
            pointer to the previous allocated block */
         pool->used = align_datasize(sizeof(void *));
      }
      /* the last allocated block exists and it has enough space to
         allocate an atom */
      ptr = (void *)((char *)pool->link + pool->used);
      pool->used += pool->size;
done: pool->count++;
#if 1
      memset(ptr, '?', pool->size);
#endif
      return ptr;
}

/*----------------------------------------------------------------------
-- get_atomv - allocate atom of variable size.
--
-- *Synopsis*
--
-- #include "glpset.h"
-- void *get_atomv(POOL *pool, int size);
--
-- *Description*
--
-- The routine get_atomv allocates an atom (i.e. continuous space of
-- memory) using the specified memory pool. It is assumed that the pool
-- was created by the routine create_pool with size = 0. The actual
-- size (in bytes) of atom which should be allocated is specified by
-- size (it should be positive and not greater than 256).
--
-- Note that being allocated the atom initially contains arbitrary data
-- (not binary zeros).
--
-- *Returns*
--
-- The routine get_atomv returns a pointer to the allocated atom. */

void *get_atomv(POOL *pool, int size)
{     void *ptr;
      if (pool->size != 0)
         fault("get_atomv: pool cannot be used to allocate an atom of v"
            "ariable size");
      if (!(1 <= size && size <= 256))
         fault("get_atomv: invalid atom size");
      /* actual atom size should be not less than sizeof(void *) and
         should be properly aligned */
      if (size < sizeof(void *)) size = sizeof(void *);
      size = align_datasize(size);
      /* if the last allocated block does not exist or if it has not
         enough space, we need a new block */
      if (pool->link == NULL || pool->used + size > POOL_SIZE)
      {  /* we can pull a new block from the list of free blocks, or if
            this list is empty, we need to allocate such block */
         if (pool->stock != NULL)
         {  ptr = pool->stock;
            pool->stock = *(void **)ptr;
         }
         else
            ptr = umalloc(POOL_SIZE);
         /* the new block becomes the last allocated block */
         *(void **)ptr = pool->link;
         pool->link = ptr;
         /* now only few bytes in the new block are used to hold a
            pointer to the previous allocated block */
         pool->used = align_datasize(sizeof(void *));
      }
      /* the last allocated block exists and it has enough space to
         allocate an atom */
      ptr = (void *)((char *)pool->link + pool->used);
      pool->used += size;
      pool->count++;
#if 1
      memset(ptr, '?', size);
#endif
      return ptr;
}

/*----------------------------------------------------------------------
-- free_atom - free an atom.
--
-- *Synopsis*
--
-- #include "glpset.h"
-- void free_atom(POOL *pool, void *ptr);
--
-- *Description*
--
-- The routine free_atom frees an atom pointed to by ptr, returning
-- this atom to the free atom list of the specified pool. Assumed that
-- the atom was allocated from the same pool by the routine get_atom,
-- otherwise the behavior is undefined. */

void free_atom(POOL *pool, void *ptr)
{     if (pool->size == 0)
         fault("free_atom: pool cannot be used to free an atom");
      if (pool->count == 0)
         fault("free_atom: pool allocation error");
      /* return the atom to the list of free atoms */
      *(void **)ptr = pool->avail;
      pool->avail = ptr;
      pool->count--;
      return;
}

/*----------------------------------------------------------------------
-- clear_pool - free all atoms.
--
-- *Synopsis*
--
-- #include "glpset.h"
-- void clear_pool(POOL *pool);
--
-- *Description*
--
-- The routine clear_pool frees all atoms borrowed from the specified
-- memory pool by means of the routines get_atom or get_atomv. Should
-- note that the clear_pool routine performs this operation moving all
-- allocated blocks to the list of free blocks, hence no memory will
-- be returned to the control program. */

void clear_pool(POOL *pool)
{     void *ptr;
      /* all allocated blocks are moved to the list of free blocks */
      while (pool->link != NULL)
      {  ptr = pool->link;
         pool->link = *(void **)ptr;
         *(void **)ptr = pool->stock;
         pool->stock = ptr;
      }
      pool->avail = NULL;
      pool->used = 0;
      pool->count = 0;
      return;
}

/*----------------------------------------------------------------------
-- delete_pool - delete memory pool.
--
-- *Synopsis*
--
-- #include "glpset.h"
-- void delete_pool(POOL *pool);
--
-- *Description*
--
-- The routine delete_pool deletes the specified memory pool, returning
-- all memory allocated to the pool to the control program. */

void delete_pool(POOL *pool)
{     void *ptr;
      clear_pool(pool);
      /* now all blocks belong to the free block list */
      while (pool->stock != NULL)
      {  ptr = pool->stock;
         pool->stock = *(void **)ptr;
         ufree(ptr);
      }
      ufree(pool);
      return;
}

/* eof */
