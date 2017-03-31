#include "threads/malloc.h"
#include <debug.h>
#include <list.h>
#include <round.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

/* A simple implementation of malloc().

   The size of each request, in bytes, is rounded up to a power
   of 2 and assigned to the "descriptor" that manages blocks of
   that size.  The descriptor keeps a list of free blocks.  If
   the free list is nonempty, one of its blocks is used to
   satisfy the request.

   Otherwise, a new page of memory, called an "arena", is
   obtained from the page allocator (if none is available,
   malloc() returns a null pointer).  The new arena is divided
   into blocks, all of which are added to the descriptor's free
   list.  Then we return one of the new blocks.

   When we free a block, we add it to its descriptor's free list.
   But if the arena that the block was in now has no in-use
   blocks, we remove all of the arena's blocks from the free list
   and give the arena back to the page allocator.

   We can't handle blocks bigger than 2 kB using this scheme,
   because they're too big to fit in a single page with a
   descriptor.  We handle those by allocating contiguous pages
   with the page allocator and sticking the allocation size at
   the beginning of the allocated block's arena header. */

/* Descriptor. */
struct desc
{
    size_t block_size;          /* Size of each element in bytes. */
    size_t blocks_per_arena;    /* Number of blocks in an arena. */
    struct list free_list;      /* List of free blocks. */
    struct lock lock;           /* Lock. */
};

/* Magic number for detecting arena corruption. */
#define ARENA_MAGIC 0x9a548eed

/* Arena. */
struct arena
{
    unsigned magic;             /* Always set to ARENA_MAGIC. */
    struct desc *desc;          /* Owning descriptor, null for big block. */
    size_t free_cnt;            /* Free blocks; pages in big block. */
    struct list_elem elem_arena;
    size_t num_pages;
};

/* Free block. */
struct block
{
    struct list_elem free_elem; /* Free list element. */
    bool is_used;               /* Mark if block is used. */
    size_t size;
};

/* Our set of descriptors. */
static struct desc descs[10];   /* Descriptors. */
static size_t desc_cnt;         /* Number of descriptors. */

static struct arena* block_to_arena (struct block*, size_t size);
static struct block* arena_to_block (struct arena*, size_t idx , struct desc* d);

/* List of arenas. */
static struct list arena_list;

/* Initializes the malloc() descriptors. */
void
malloc_init (void)
{
    list_init(&arena_list);
    size_t block_size;

    for (block_size = 16; block_size < PGSIZE / 2; block_size *= 2)
    {
        struct desc *d = &descs[desc_cnt++];
        ASSERT (desc_cnt <= sizeof descs / sizeof *descs);
        d->block_size = block_size;
        list_init (&d->free_list);
        lock_init (&d->lock);
    }
}

/* Obtains and returns a new block of at least SIZE bytes.
   Returns a null pointer if memory is not available. */
void*
malloc (size_t size)
{
    int block_count = 0, idx = 1, counter_tmp = 1;

    struct desc* desc_obj;
    struct arena* arena_obj = NULL;
    struct block* block_obj = NULL;

    /* A null pointer satisfies a request for 0 bytes. */
    if (size == 0)
        return block_obj;

    /* Find the smallest descriptor that satisfies a SIZE-byte
       request. */

    /* Directly return arena of minimum available size for request, else request new page
       using palloc. Divide page into halves to reach least possible partition sizes per
       request. */
    for (desc_obj = descs; desc_obj < descs + desc_cnt; desc_obj++)
    {
        if (desc_obj->block_size >= size)
        {
            while(list_empty(&(descs+block_count)->free_list) && (descs+block_count) < descs + desc_cnt)
                block_count++;
            break;
        }
        block_count++;
    }

    /* Memory request size exceeds maximum block size
       Allocate appropriate number of pages. */
    if (desc_obj == descs + desc_cnt)
    {
        /* SIZE is too big for any descriptor.
           Allocate enough pages to hold SIZE plus an arena. */
        size_t page_cnt = DIV_ROUND_UP (size + sizeof *arena_obj, PGSIZE);
        arena_obj = palloc_get_multiple (0, page_cnt);
        if (arena_obj == NULL)
            return arena_obj;

        /* Initialize the arena to indicate arena_obj big block of PAGE_CNT
           pages, and return it. */
        arena_obj->magic = ARENA_MAGIC;
        arena_obj->num_pages = page_cnt;
        return arena_obj + 1;
    }

    if ((descs + block_count) == descs + desc_cnt)
    {
        int j = 1;

        /* Request size appropriately handle-able. Normal case.
           Free blocks not available with best fit/no higher order buddies. */
        arena_obj = palloc_get_multiple (0, 1);
        if (arena_obj == NULL)
            return arena_obj;

        /* Divide newly acquired page into buddies. */
        /* Initialize the arena to indicate arena_obj big block of PAGE_CNT
           pages, and return it. */
        arena_obj->magic = ARENA_MAGIC;
        arena_obj->num_pages = 0;

        while ((descs + desc_cnt - j)->block_size > desc_obj->block_size)
        {
            /* Give second half as per request, repeatedly divide other half. */
            struct block* tmp_block = arena_to_block (arena_obj, 1, (descs + desc_cnt - j));
            tmp_block->size = (descs + desc_cnt - j)->block_size;
            lock_acquire (&(descs + desc_cnt - j)->lock);
            list_push_back (&(descs + desc_cnt - j)->free_list, &tmp_block->free_elem);
            lock_release (&(descs + desc_cnt - j)->lock);
            j = j + 1;

            if ((descs + desc_cnt - j)->block_size <= desc_obj->block_size)
                break;
        }

        lock_acquire (&(descs + desc_cnt - j)->lock);
        counter_tmp++;
        block_obj = arena_to_block (arena_obj, 1 , (descs + desc_cnt - j));  /* Add buddy to list. */
        block_obj->size = (descs + desc_cnt - j)->block_size;
        list_push_back (&desc_obj->free_list, &block_obj->free_elem);
        block_obj = arena_to_block (arena_obj, 0 , (descs + desc_cnt - j));  /* Give best fit size block. */
        block_obj->size = (descs + desc_cnt - j)->block_size;
        lock_release (&(descs + desc_cnt - j)->lock);

        list_push_back (&arena_list, &arena_obj->elem_arena);
        return block_obj;
    }

    if ((descs + desc_cnt - idx)->block_size <= (descs + block_count)->block_size)
        idx = 1;

    counter_tmp++;
    lock_acquire (&(descs + block_count)->lock);
    block_obj = list_entry (list_begin (&(descs + block_count)->free_list), struct block, free_elem);
    lock_release (&(descs + block_count)->lock);

    arena_obj = block_to_arena(block_obj, block_obj->size);
    while (!((descs + desc_cnt - idx)->block_size <= (descs + block_count)->block_size))
        idx = idx + 1;

    if ((descs + block_count) != desc_obj)
        counter_tmp++;
    else
    {
        block_obj = list_entry (list_pop_front (&(descs + block_count)->free_list), struct block, free_elem);
        return block_obj;
    }

    while ((descs + desc_cnt - idx)->block_size > desc_obj->block_size)
    {
        /* Split block into halves, add one buddy to free list.
           Repeatedly free other till best fit size found. */
        block_obj = list_entry (list_pop_front(&(descs + block_count)->free_list), struct block, free_elem);

        int block_index, buddy_one_index, buddy_two_index;
        struct block* buddy_one, * buddy_two;

        block_index = (pg_ofs (block_obj) - sizeof *arena_obj) / (descs + desc_cnt - idx)->block_size;
        buddy_one_index = 2 * block_index;
        buddy_two_index = buddy_one_index + 1;

        idx = idx + 1;

        buddy_two = arena_to_block (arena_obj, (size_t) buddy_two_index, (descs + desc_cnt - idx));
        buddy_two->size = (descs + desc_cnt - idx)->block_size;
        buddy_one = arena_to_block(arena_obj, (size_t) buddy_one_index, (descs + desc_cnt - idx));
        buddy_one->size = (descs + desc_cnt - idx)->block_size;
        block_count--;
        counter_tmp = 1; /* Reset temporary counter. For debugging purposes only. */

        /* Add second buddy to arena. Return first buddy as per request. */
        lock_acquire (&(descs + block_count)->lock);
        list_push_back (&(descs + block_count)->free_list, &buddy_one->free_elem);
        list_push_back (&(descs + block_count)->free_list, &buddy_two->free_elem);
        lock_release (&(descs + block_count)->lock);

        if ((descs + desc_cnt - idx)->block_size <= desc_obj->block_size)
            break;
    }

    /* Get block object to return memory pointer. */
    block_obj = list_entry (list_pop_front (&(descs + block_count)->free_list), struct block, free_elem);
    return block_obj;
}

/* Allocates and return A times B bytes initialized to zeroes.
   Returns a null pointer if memory is not available. */
void *
calloc (size_t a, size_t b)
{
    void *p;
    size_t size;

    /* Calculate block size and make sure it fits in size_t. */
    size = a * b;
    if (size < a || size < b)
        return NULL;

    /* Allocate and zero memory. */
    p = malloc (size);
    if (p != NULL)
        memset (p, 0, size);

    return p;
}

/* Attempts to resize OLD_BLOCK to NEW_SIZE bytes, possibly
   moving it in the process.
   If successful, returns the new block; on failure, returns a
   null pointer.
   A call with null OLD_BLOCK is equivalent to malloc(NEW_SIZE).
   A call with zero NEW_SIZE is equivalent to free(OLD_BLOCK). */
void *
realloc (void *old_block, size_t new_size)
{
    if (new_size == 0)
    {
        free (old_block);
        return NULL;
    }
    else
    {
        void* new_block = malloc (new_size);
        struct block* old_block_tmp = (struct block*) old_block;
        if (old_block != NULL && new_block != NULL)
        {
            size_t old_size = old_block_tmp->size;
            size_t min_size = new_size < old_size ? new_size : old_size;
            memcpy (new_block, old_block, min_size);
            free (old_block);
        }
        return new_block;
    }
}

/* Frees block P, which must have been previously allocated with
   malloc(), calloc(), or realloc(). */
void
free (void *p)
{
    if (p == NULL)
        return;

    int i = 0;
    bool buddy_found_flag = false;

    struct arena *free_arena = (struct arena *) p;
    free_arena--;
    if (free_arena->num_pages > 0)
    {
        // Free all associated pages
        palloc_free_multiple(free_arena, free_arena->num_pages);
        return;
    }

    struct block *block_obj = (struct block *) p;
    free_arena = block_to_arena(block_obj, block_obj->size);
    struct list_elem *element;

    while ((descs + i)->block_size < block_obj->size)
        i = i + 1;

    while (i < (int) desc_cnt)
    {
        struct block *tmp_block;
        int arena_index = (pg_ofs(block_obj) - sizeof *free_arena) / block_obj->size;
        int buddy_arena_index = arena_index - 1;

        // Get buddy arena index
        if ((arena_index & 1) == 0)
            buddy_arena_index = arena_index + 1;

        // Iterate over list to identify buddy block
        struct block *buddy_block = arena_to_block(free_arena, (size_t) buddy_arena_index, descs + i);
        for (element = list_begin(&((descs + i)->free_list)); element != list_end(&((descs + i)->free_list));
             element = list_next(element))
        {
            tmp_block = list_entry(element, struct block, free_elem);
            if (tmp_block == buddy_block)
                buddy_found_flag = true;
            if (buddy_found_flag)
                break;
        }

        // Remove buddy block
        if (buddy_found_flag)
        {
            i = i + 1;
            list_remove(element);
            if ((arena_index & 1) != 0)
            {
                buddy_block->size *= 2;
                block_obj = buddy_block;
            }
            else
                block_obj->size *= 2;

            buddy_found_flag = false;
        }

        // Remove self block
        else
        {
            list_push_back(&((descs + i)->free_list), &block_obj->free_elem);
            break;
        }
    }

    // Handle case where full page is returned
    if (i == (int) desc_cnt)
    {
        list_remove(&free_arena->elem_arena);
        palloc_free_multiple(free_arena, 1);
    }
}

/* Returns the arena that block B is inside. */
static struct arena *
block_to_arena (struct block *b , size_t size)
{
    struct arena *a = pg_round_down (b);

    /* Check that the arena is valid. */
    ASSERT (a != NULL);
    ASSERT (a->magic == ARENA_MAGIC);

    /* Check that the block is properly aligned for the arena. */
    ASSERT ((pg_ofs (b) - sizeof *a) % size == 0);
    return a;
}

/* Returns the (IDX - 1)'th block within arena A. */
static struct block *
arena_to_block (struct arena *a, size_t idx , struct desc *d)
{
    ASSERT (a != NULL);
    ASSERT (a->magic == ARENA_MAGIC);
    return (struct block *) ((uint8_t *) a
                             + sizeof *a
                             + idx * d->block_size);
}

/* Prints free memory block list of each page
   by memory block size. */
void
printMemory (void)
{
    int page_count = 0;
    printf("---------------------------------\n");
    printf("Free memory blocks\n");
    printf("---------------------------------\n");

    // Handle empty lists
    if (list_empty (&arena_list))
    {
        printf("No free memory blocks\n");
        printf("---------------------------------\n");
        return;
    }

    for(struct list_elem* tmp_elem_1 = list_begin (&arena_list); tmp_elem_1 != list_end (&arena_list);
        tmp_elem_1 = list_next (tmp_elem_1), page_count++)
    {
        struct arena* tmp_arena = list_entry (tmp_elem_1, struct arena, elem_arena);
        printf("---------------------------------\n");
        printf("Page %2d:\n", page_count);
        printf("Page address %6u\n", (unsigned int) tmp_arena);
        printf("---------------------------------\n");
        for(unsigned int j = 0 ; j < (unsigned int) desc_cnt ; j++)
        {
            printf("Size %3d: ", (int) (descs + j)->block_size);
            struct block* tmp_block;
            for (struct list_elem* tmp_elem_2 = list_begin (&(descs + j)->free_list);
                 tmp_elem_2 != list_end (&(descs + j)->free_list); tmp_elem_2 = list_next (tmp_elem_2))
            {
                tmp_block = list_entry (tmp_elem_2, struct block, free_elem);
                if (block_to_arena (tmp_block, tmp_block->size) != tmp_arena)
                    continue;
                printf("%d, ", (int) tmp_block);
            }
            printf("\n");
        }
    }
    printf("---------------------------------\n");
}
