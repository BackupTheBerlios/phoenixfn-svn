/*
 *
 * File:   blalloc.c
 * Owner:  Wohali (Joan Touzet)
 *
 */

#include "blalloc.h"
#include "ircd_defs.h"
#include "irc_string.h"
#include "numeric.h"
#include "umodes.h"
#include "list.h"
#include "s_log.h"

#include <string.h>
#include <stdlib.h>
#include <assert.h>

/**************************************************************************
 * FUNCTION DOCUMENTATION:                                                *
 *    newblock                                                            *
 * Description:                                                           *
 *    mallocs a new block for addition to a blockheap                     *
 * Parameters:                                                            *
 *    bh (IN): Pointer to parent blockheap.                               *
 * Note: This function will not return if it fails                        *
 **************************************************************************/
static void newblock(BlockHeap *bh)
{
   Block *b;

   /* Setup the initial data structure. */
   expect_malloc;
   b = (Block *) MyMalloc (sizeof(Block));
   malloc_log("newblock(), allocated Block (%zd bytes) at %p", sizeof(Block), (void *)b);
   if (b == NULL)
     outofmemory();

   b->freeElems = bh->elemsPerBlock;
   b->next = bh->base;
   expect_malloc;
   b->allocMap = MyMalloc (sizeof(unsigned long) * (bh->numlongs +1));
   malloc_log("newblock(), allocated %d longs (%zd bytes) at %p", bh->numlongs + 1,
              sizeof(unsigned long) * (bh->numlongs + 1), (void *)b->allocMap);

   if (b->allocMap == NULL)
     outofmemory();
   memset(b->allocMap, 0, (bh->numlongs + 1 ) * sizeof(unsigned long));
   
   /* Now allocate the memory for the elems themselves. */
   expect_malloc;
   b->elems = MyMalloc ((bh->elemsPerBlock + 1) * bh->elemSize);
   malloc_log("newblock(), allocated %d elems of %zd (%zd bytes) at %p", bh->elemsPerBlock + 1,
              bh->elemSize, (bh->elemsPerBlock + 1) * bh->elemSize, b->elems);
   if (b->elems == NULL)
     outofmemory();

   b->endElem = (void *)((unsigned long) b->elems +
       (unsigned long) ((bh->elemsPerBlock - 1) * bh->elemSize));

   /* Finally, link it in to the heap. */
   ++bh->blocksAllocated;
   bh->freeElems += bh->elemsPerBlock;
   bh->base = b;
   
   return;
}


/* ************************************************************************ */
/* FUNCTION DOCUMENTATION:                                                  */
/*    BlockHeapCreate                                                       */
/* Description:                                                             */
/*   Creates a new blockheap from which smaller blocks can be allocated.    */
/*   Intended to be used instead of multiple calls to malloc() when         */
/*   performance is an issue.                                               */
/* Parameters:                                                              */
/*   elemsize (IN):  Size of the basic element to be stored                 */
/*   elemsperblock (IN):  Number of elements to be stored in a single block */
/*         of memory.  When the blockheap runs out of free memory, it will  */
/*         allocate elemsize * elemsperblock more.                          */
/* Returns:                                                                 */
/*   Pointer to new BlockHeap, or NULL if unsuccessful                      */
/* ************************************************************************ */
BlockHeap *BlockHeapCreate (size_t elemsize,
                            int elemsperblock)
{
   BlockHeap *bh;

   /* Catch idiotic requests up front */
   if ((elemsize <= 0) || (elemsperblock <= 0))
     outofmemory();

   /* Allocate our new BlockHeap */
   expect_malloc;
   bh = (BlockHeap *) MyMalloc(sizeof(BlockHeap));
   malloc_log("BlockHeapCreate(), allocated BlockHeap (%zd bytes) at %p",
              sizeof(BlockHeap), (void *)bh);
   
   if (bh == NULL) 
     outofmemory();

   elemsize = elemsize + (elemsize & (sizeof(void *) - 1));
   bh->elemSize = elemsize;
   bh->elemsPerBlock = elemsperblock;
   bh->blocksAllocated = 0;
   bh->freeElems = 0;
   bh->numlongs = (bh->elemsPerBlock / (sizeof(long) * 8)) + 1;
   if ( (bh->elemsPerBlock % (sizeof(long) * 8)) == 0)
     bh->numlongs--;
   bh->base = NULL;

   /* Be sure our malloc was successful */
   newblock(bh);
   assert(bh);

   return bh;
}

/* ************************************************************************ */
/* FUNCTION DOCUMENTATION:                                                  */
/*    BlockHeapAlloc                                                        */
/* Description:                                                             */
/*    Returns a pointer to a struct within our BlockHeap that's free for    */
/*    the taking.                                                           */
/* Parameters:                                                              */
/*    bh (IN):  Pointer to the Blockheap.                                   */
/* Returns:                                                                 */
/*    Pointer to a structure (void *), or NULL if unsuccessful.             */
/* ************************************************************************ */

void *BlockHeapAlloc (BlockHeap *bh)
{
  Block *walker;
  int unit;
  unsigned long mask;
  unsigned long ctr;

  if (bh == NULL)
    abort();

  if (bh->freeElems == 0)
    {
      newblock(bh);
      walker = bh->base;
      walker->allocMap[0] = 0x1L;
      walker->freeElems--;  bh->freeElems--;
      assert(bh->base->elems);

      return bh->base->elems;      /* ...and take the first elem. */
    }

  for (walker = bh->base; walker; walker = walker->next)
    {
      if (walker->freeElems > 0)
        {
          mask = 0x1L; ctr = 0; unit = 0;
          while (unit < bh->numlongs)
            {
              if ((mask == 0x1UL) && (walker->allocMap[unit] == ~0UL))
                {
                  /* Entire subunit is used, skip to next one. */
                  unit++;
                  ctr = 0;
                  continue;
                }
              /* Check the current element, if free allocate block */
              if (!(mask & walker->allocMap[unit]))
                {
                  walker->allocMap[unit] |= mask; /* Mark block as used */
                  walker->freeElems--;  bh->freeElems--;
                  /* And return the pointer */

                  /* Address arithemtic is always ca-ca 
                   * have to make sure the the bit pattern for the
                   * base address is converted into the same number of
                   * bits in an integer type, that has at least
                   * sizeof(unsigned long) at least == sizeof(void *)
                   * -Dianora 
                   */

                  return ( (void *) (
                                     (unsigned long)walker->elems + 
                                     ( (unit * sizeof(unsigned long) * 8 + ctr)
                                       * (unsigned long )bh->elemSize))
                           );

                }
              /* Step up to the next unit */
              mask <<= 1;
              ctr++;
              if (!mask)
                {
                  mask = 0x1L;
                  unit++;
                  ctr = 0;
                }
            }  /* while */
        }     /* if */
    }        /* for */

  abort();
}

/* ************************************************************************ */
/* FUNCTION DOCUMENTATION:                                                  */
/*    BlockHeapFree                                                         */
/* Description:                                                             */
/*    Returns an element to the free pool, does not free()                  */
/* Parameters:                                                              */
/*    bh (IN): Pointer to BlockHeap containing element                      */
/*    ptr (in):  Pointer to element to be "freed"                           */
/* ************************************************************************ */
void BlockHeapFree(BlockHeap *bh, void *ptr)
{
   Block *walker;
   unsigned long ctr;
   unsigned long bitmask;

   if (bh == NULL)
     abort();

   for (walker = bh->base; walker != NULL; walker = walker->next)
     {
      if ((ptr >= walker->elems) && (ptr <= walker->endElem))
        {
          ctr = ((unsigned long) ptr - 
                 (unsigned long) (walker->elems))
            / (unsigned long )bh->elemSize;

          bitmask = 1L << (ctr % (sizeof(long) * 8));
          ctr = ctr / (sizeof(long) * 8);
          /* Flip the right allocation bit */
          /* Complain if the bit is already clear, something is wrong
           * (typically, someone freed the same block twice)
           */

          if(!(walker->allocMap[ctr] & bitmask))
            abort();
          else
            {
              walker->allocMap[ctr] = walker->allocMap[ctr] & ~bitmask;
              walker->freeElems++;  bh->freeElems++;
            }
          return;
        }
     }
   abort();
}

/* ************************************************************************ */
/* FUNCTION DOCUMENTATION:                                                  */
/*    BlockHeapGarbageCollect                                               */
/* Description:                                                             */
/*    Performs garbage colletion on the block heap.  Any blocks that are    */
/*    completely unallocated are removed from the heap.  Garbage collection */
/*    will never remove the root node of the heap.                          */
/* Parameters:                                                              */
/*    bh (IN):  Pointer to the BlockHeap to be cleaned up                   */
/* ************************************************************************ */
void BlockHeapGarbageCollect(BlockHeap *bh)
{
   Block *walker, *last;

   if (bh == NULL)
     abort();

   if (bh->freeElems < bh->elemsPerBlock)
     /* There couldn't possibly be an entire free block.  Return. */
     return;

   last = NULL;
   walker = bh->base;

   /* This section rewritten Dec 10 1998 - Dianora */
   while(walker)
     {
       int i;
       for (i = 0; i < bh->numlongs; i++)
         {
           if (walker->allocMap[i])
             break;
         }
       if (i == bh->numlongs)
         {
           /* This entire block is free.  Remove it. */
           MyFree(walker->elems);
           MyFree(walker->allocMap);

           if (last)
             {
               last->next = walker->next;
               MyFree(walker);
               walker = last->next;
             }
           else
             {
               bh->base = walker->next;
               MyFree(walker);
               walker = bh->base;
             }
           bh->blocksAllocated--;
           bh->freeElems -= bh->elemsPerBlock;
         }
       else
         {
           last = walker;
           walker = walker->next;
         }
     }
   return;
}


/* ************************************************************************ */
/* FUNCTION DOCUMENTATION:                                                  */
/*    BlockHeapDestroy                                                      */
/* Description:                                                             */
/*    Completely free()s a BlockHeap.  Use for cleanup.                     */
/* Parameters:                                                              */
/*    bh (IN):  Pointer to the BlockHeap to be destroyed.                   */
/* ************************************************************************ */
void BlockHeapDestroy(BlockHeap *bh)
{
  Block *walker, *next;

   if (bh == NULL)
     abort();

   for (walker = bh->base; walker != NULL; walker = next)
     {
       next = walker->next;
       MyFree(walker->elems);
       MyFree(walker->allocMap);
       MyFree(walker);
     }

   MyFree (bh);

   return;
}

/* ************************************************************************ */
/* FUNCTION DOCUMENTATION:                                                  */
/*    BlockHeapCountMemory                                                  */
/* Description:                                                             */
/*    Counts up memory used by heap, and memory allocated out of heap       */
/* Parameters:                                                              */
/*    bh (IN):  Pointer to the BlockHeap to be counted.                     */
/*    TotalUsed (IN): Pointer to int, total memory used by heap             */
/*    TotalAllocated (IN): Pointer to int, total memory allocated           */
/* Returns:                                                                 */
/*   TotalUsed                                                              */
/*   TotalAllocated                                                         */
/* ************************************************************************ */

void BlockHeapCountMemory(BlockHeap *bh, size_t *TotalUsed, size_t *TotalAllocated, size_t *overheads)
{
  Block *walker;

  *TotalUsed = 0;
  *TotalAllocated = 0;
  *overheads = 0;

  if (bh == NULL)
    return;

  *overheads = sizeof(BlockHeap);

  for (walker = bh->base; walker != NULL; walker = walker->next)
    {
      *overheads += sizeof(Block);
      /* b->allocmap */
      *overheads += sizeof(unsigned long) * (bh->numlongs + 1);
      /* b->elems */
      *TotalAllocated += (bh->elemsPerBlock + 1) * bh->elemSize;
      /* elements which have been used (by BlockHeapAlloc()) */
      *TotalUsed += ((bh->elemsPerBlock - walker->freeElems)
		     * bh->elemSize);
    }
}
