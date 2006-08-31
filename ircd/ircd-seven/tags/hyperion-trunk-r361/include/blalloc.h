/*
 *
 * File:   blalloc.h
 * Owner:   Wohali (Joan Touzet)
 *
 *
 * 
 */
#ifndef INCLUDED_blalloc_h
#define INCLUDED_blalloc_h
#ifndef INCLUDED_sys_types_h
#include <sys/types.h>       /* size_t */
#define INCLUDED_sys_types_h
#endif

#ifndef INCLUDED_stddef_h
#include <stddef.h>
#define INCLUDED_stddef_h
#endif

/* 
 * Block contains status information for an allocated block in our
 * heap.
 */
struct Block {
  void*          elems;                 /* Points to allocated memory */
  void*          endElem;               /* Points to last elem for boundck */
  int            freeElems;             /* Number of available elems */
  struct Block*  next;                  /* Next in our chain of blocks */
  unsigned long* allocMap;              /* Bitmap of allocated blocks */
};

typedef struct Block Block;


/* 
 * BlockHeap contains the information for the root node of the
 * memory heap.
 */
struct BlockHeap {
   size_t  elemSize;                    /* Size of each element to be stored */
   int     elemsPerBlock;               /* Number of elements per block */
   int     numlongs;                    /* Size of Block's allocMap array */
   int     blocksAllocated;             /* Number of blocks allocated */
   int     freeElems;                   /* Number of free elements */
   Block*  base;                        /* Pointer to first block */
};

typedef struct BlockHeap BlockHeap;

extern BlockHeap* BlockHeapCreate(size_t elemsize, int elemsperblock);
extern void       BlockHeapDestroy(BlockHeap *bh);
extern void*      BlockHeapAlloc(BlockHeap *bh);
extern void       BlockHeapFree(BlockHeap *bh, void *ptr);
extern void       BlockHeapGarbageCollect(BlockHeap *);

extern void       BlockHeapCountMemory(BlockHeap *bh, size_t *, size_t *, size_t *);

#define BlockHeapALLOC(bh, type)  ((type*) BlockHeapAlloc(bh))

#endif /* INCLUDED_blalloc_h */

