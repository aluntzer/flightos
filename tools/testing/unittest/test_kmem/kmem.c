/**
 * @file kernel/kmem.c
 *
 * @ingroup kmem
 * @defgroup kmem Kernel Memory Allocator
 *
 * @brief a malloc-like kernel memory allocator 
 *
 * A simple high-level allocator that uses kernel_sbrk() implemented by the
 * architecture-specific (MMU) code to allocate memory. If no MMU is available,
 * it falls back to using the architecture's boot memory allocator.
 *
 * This is similar to @ref chunk, but a lot simpler. This needs to respect
 * sbrk() so it can't just use @chunk because it may release any parent chunk,
 * while we need to do that from _kmem_last to _kmem_init as they become free.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "list.h"


void kfree(void *ptr);

#define ALIGN_MASK(x, mask)    (((x) + (mask)) & ~(mask))
#define ALIGN(x, a)            ALIGN_MASK(x, (typeof(x))(a) - 1)
#define ALIGN_PTR(x, a)        (typeof(x)) ALIGN((unsigned long) x, a)
#define IS_ALIGNED(x, a)       (((x) & ((typeof(x))(a) - 1)) == 0)


#define WORD_ALIGN(x)	ALIGN((x), sizeof(uint64_t))

#define MAGIC		0xB19B00B5
#define FREE_MAGIC	0x0DEFACED

#define CONFIG_MMU 1;


#ifdef CONFIG_MMU
struct kmem {
	void *data;
	size_t size;
	int free;
	struct kmem *prev, *next;
	struct list_head node;
	int magic;	/* might as well use the alignment word for something */
} __attribute__((aligned(8)));

/* the initial and the most recent allocation */
static struct kmem *_kmem_init;
static struct kmem *_kmem_last;


#define likely(x)      __builtin_expect(!!(x), 1)


#define STACK_ALIGN	8
#define PAGE_SIZE	4096
#define PAGE_ALIGN(addr)	ALIGN(addr, PAGE_SIZE)
#define MEMSIZE_MAX 128*1024*1024
uint32_t *mem_base;
uint32_t addr_lo;
uint32_t addr_hi;

unsigned long sbrk;


size_t n_free;
size_t n_alloc;



void *memset32(void *s, uint32_t c, size_t n)
{
        uint32_t *p = s;

        while (n--)
                *p++ = c;

        return s;
}

void *kernel_sbrk(intptr_t increment)
{
        long brk;
        long oldbrk;

        unsigned long ctx;


        if (!increment)
                 return (void *) sbrk;

        oldbrk = sbrk;

        /* make sure we are always double-word aligned, otherwise we can't
         * use ldd/std instructions without trapping */
        increment = ALIGN(increment, STACK_ALIGN);

        brk = oldbrk + increment;

        if (brk < addr_lo)
                return (void *) -1;

        if (brk > addr_hi)
                return (void *) -1;

        /* try to release pages if we decremented below a page boundary */
        if (increment < 0) {
                if (PAGE_ALIGN(brk) < PAGE_ALIGN(oldbrk - PAGE_SIZE)) {
#if 0
                        printf("SBRK: release %lx (va %lx, pa%lx) to va %lx pa %lx, %d pages\n",
                                 brk, PAGE_ALIGN(brk), PAGE_ALIGN(brk),
                                 oldbrk, oldbrk,
                                 (PAGE_ALIGN(oldbrk) - PAGE_ALIGN(brk)) / PAGE_SIZE);
#endif

			/* mark unused */
                        if (PAGE_ALIGN(oldbrk) >  PAGE_ALIGN(brk)) {
				/* we legally own more memory, so setting n+1 is
				 * fine
				 */
				int i;
				unsigned char *p = (unsigned char *) brk;
#if 1
				for (i = 0; i < oldbrk - brk; i++) {
#if 0
					printf("set %p\n", &p[i]);
#endif

					p[i] = 0x55;
				}
#endif
			}
                }
        }

        sbrk = brk;
#if 0
        printf("SBRK: moved %08lx -> %08lx, alloc %g MB\n", oldbrk, brk, (brk-addr_lo) / (1024. * 1024.));
#else
#if 0
        printf("%g\n", (brk-addr_lo) / (1024. * 1024.));
#endif
#endif

        return (void *) oldbrk;
}


/**
 * @brief see if we can find a suitable chunk in our pool
 */

static struct kmem *kmem_find_free_chunk(size_t size)
{
	struct kmem *p_tmp;
	struct kmem *p_elem;



	if (list_empty(&_kmem_init->node))
		return NULL;

	list_for_each_entry_safe(p_elem, p_tmp, &_kmem_init->node, node) {

		if (p_elem->size < size)
			continue;

		list_del_init(&p_elem->node);

		return p_elem;
	}

	return NULL;
}


/**
 * @brief split a chunk in two for a given size
 */

static void kmem_split(struct kmem *k, size_t size)
{
	size_t len;
	struct kmem *split;


	/* align first */
	split = (struct kmem *)((size_t) k->data + size);
	split = ALIGN_PTR(split, sizeof(uint64_t));

	split->data = split + 1;

	len = ((uintptr_t) split - (uintptr_t) k->data);

	/* now check if we still fit the required size */
	if (k->size < len + sizeof(*split))
		return;

	split->size = k->size - len - sizeof(*split);

	/* we're good, finalise the split */
	split->free = FREE_MAGIC;
	split->magic = 0;
	split->prev = k;
	split->next = k->next;

	k->size = ((uintptr_t) split - (uintptr_t) k->data);

	if (k->next)
		k->next->prev = split;

	k->next = split;

	if (!split->next)
		_kmem_last = split;

	list_add_tail(&split->node, &_kmem_init->node);
}


/**
 * @brief merge a chunk with its neighbour
 */

static void kmem_merge(struct kmem *k)
{
	k->size = k->size + k->next->size + sizeof(struct kmem);

	k->next = k->next->next;

	if (k->next)
		k->next->prev = k;
}

#endif /* CONFIG_MMU */





/**
 * @brief returns the initial kmem chunk
 *
 * @returns pointer or NULL on error
 */

void *kmem_init(void)
{
#ifdef CONFIG_MMU
	if (likely(_kmem_init))
		return _kmem_init;

	_kmem_init = kernel_sbrk(WORD_ALIGN(sizeof(struct kmem)));

	printf("fetch 0x%x bytes on init\n", sizeof(struct kmem));

	if (_kmem_init == (void *) -1) {
		printf("KMEM: error, cannot _kmem_initialise\n");
		return NULL;
	}

	_kmem_init->data = NULL;
	_kmem_init->size = 0;
	_kmem_init->free = 0;
	_kmem_init->magic = MAGIC;
	_kmem_init->prev = NULL;
	_kmem_init->next = NULL;

	/* we track our free chunks in the node of the initial allocation */
	INIT_LIST_HEAD(&_kmem_init->node);

	_kmem_last = _kmem_init;

	return _kmem_init;
#else
	return (void *) -1; /* return something not NULL */
#endif
}


/**
 * @brief allocates size bytes and returns a pointer to the allocated memory,
 *	  suitably aligned for any built-in type
 *
 * @param size the number of bytes to allocate
 *
 * @returns a pointer or NULL on error or size == 0
 */

void *kmalloc(size_t size)
{
#ifdef CONFIG_MMU
	size_t len;

	struct kmem *k_new;
	struct kmem *k_prev = _kmem_last;


	if (!size)
		return NULL;

	len = WORD_ALIGN(size + sizeof(struct kmem));



	/* try to locate a free chunk first */
	k_new = kmem_find_free_chunk(len);

	if (k_new) {
		/* take only what we need */
		if ((len + sizeof(struct kmem)) < k_new->size)
			kmem_split(k_new, len);

		k_new->free = 0;
		k_new->magic = MAGIC;
		n_alloc++;
		goto exit;
	}


	/* need fresh memory */
	k_new = kernel_sbrk(len);

	if (k_new == (void *) -1)
		return NULL;

	k_new->free = 0;
	k_new->magic = MAGIC;

	k_new->next = NULL;

	/* link */
	k_new->prev = k_prev;
	k_prev->next = k_new;

	/* the actual size is defined by sbrk(), we get a guranteed minimum,
	 * but the resulting size may be larger
	 */
	k_new->size = (size_t) kernel_sbrk(0) - (size_t) k_new - sizeof(struct kmem);

	/* data section follows just after */
	k_new->data = k_new + 1;

	_kmem_last = k_new;

exit:

	n_alloc++;
	return k_new->data;
#else
	return bootmem_alloc(size);
#endif /* CONFIG_MMU */

}


/**
 * @brief allocates memory for an array of nmemb elements of size bytes each and
 *	   returns a pointer to the allocated memory. The memory is set to zero.
 *
 * @param nmemb the number of elements
 * @param size the number of bytes per element
 *
 * @returns a pointer or NULL on error or nmemb/size == 0
 */

void *kcalloc(size_t nmemb, size_t size)
{
	size_t i;
	size_t len;

	char *dst;
	void *ptr;


	len = nmemb * size;

	ptr = kmalloc(len);

	if (ptr) {
		dst = ptr;
		for (i = 0; i < len; i++)
			dst[i] = 0;
	}

	return ptr;
}


/**
 * @brief allocates size bytes and returns a pointer to the allocated memory,
 *	  suitably aligned for any built-in type. The memory is set to zero.
 *
 * @param size the number of bytes to allocate
 *
 * @returns a pointer or NULL on error or size == 0
 */

void *kzalloc(size_t size)
{
	return kcalloc(size, 1);
}


/**
 * @brief changes the size of the memory block pointed to by ptr to size bytes.
 *	  The contents will be unchanged in the range from the start of the
 *	  region up to the minimum of the old and new sizes. If the new size is
 *	  larger than the old size,the added memory will not be initialized.
 *
 * @param ptr the old memory block, if NULL, this function is equal to kmalloc()
 * @param size the number of bytes for the new block, if 0, this is equal to
 *	  kfree()
 *
 * @returns a pointer or NULL on error or size == 0
 */

void *krealloc(void *ptr, size_t size)
{
	size_t i;

	size_t len;

	char *dst;
	char *src;

	void *ptr_new;

#ifdef CONFIG_MMU
	struct kmem *k;
#endif /* CONFIG_MMU */

	if (!ptr)
		return kmalloc(size);

#ifdef CONFIG_MMU
	if (ptr < kmem_init()) {
		printf("KMEM: invalid krealloc() of addr %p below lower "
			   "bound of trackable memory in call from %p\n",
			   ptr, NULL);
		return NULL;
	}

	if (ptr > kernel_sbrk(0)) {
		printf("KMEM: invalid krealloc() of addr %p beyond system "
			   "break in call from %p\n",
			   ptr, NULL);
		return NULL;
	}


	k = ((struct kmem *) ptr) - 1;

	if (k->data != ptr) {
		printf("KMEM: invalid krealloc() of addr %p in call "
			   "from %p\n",
			   ptr, NULL);
		return NULL;
	}

#endif /* CONFIG_MMU */

	ptr_new = kmalloc(size);

	if (!ptr_new)
		return NULL;

#ifdef CONFIG_MMU
	if (k->size > size)
		len = size;
	else
		len = k->size;
#else
	/* this WILL copy out of bounds if size < original */
	len = size;
#endif /* CONFIG_MMU */

	src = ptr;
	dst = ptr_new;

	for (i = 0; i < len; i++)
		dst[i] = src[i];

	kfree(ptr);

	return ptr_new;
}


/**
 * @brief function frees the memory space pointed to by ptr, which must have
 *	  been returned by a previous call to kmalloc(), kcalloc(),
 *	  or krealloc()
 *
 * @param ptr the memory to free
 */

void kfree(void *ptr)
{
	unsigned long flags __attribute__((unused));

	struct kmem *k __attribute__((unused));
	int rel  =0;

	if (!ptr)
		return;


#ifdef CONFIG_MMU
	if (ptr < kmem_init()) {
		printf("KMEM: invalid kfree() of addr %p below lower bound "
			   "of trackable memory in call from %p\n",
			   ptr, NULL);
		return;
	}

	if (ptr > kernel_sbrk(0)) {
		printf("KMEM: invalid kfree() of addr %p beyond system "
			   "break in call from %p\n",
			   ptr, NULL);
		return;
	}


	k = ((struct kmem *) ptr) - 1;

	if (k->data != ptr) {
		printf("KMEM: invalid kfree() of addr %p in call from %p\n",
			   ptr, NULL);
		return;
	}

	if (k->magic != MAGIC) {
		printf("KMEM: invalid magic number in kfree() of addr %p in call from %p\n",
			   ptr, NULL);
		return;
	}

	/** XXX try protecting against corruption !*/
	if (k->next && k->next->free) {
		if (k->next->free != FREE_MAGIC) {

			printf("KMEM: corruption detected have invalid free block magic 0x%08x "
			        "marker when trying to free chunk %p with next chunk %p",
				k->next->free, k, k->next);

			k->next = NULL;
		}
	}


	n_free++;
	k->free = FREE_MAGIC;
	k->magic = 0;

	if (k->next && k->next->free) {
		list_del(&k->next->node);

		kmem_merge(k);
		INIT_LIST_HEAD(&k->node);
	}

	if (k->prev->free) {

		list_del(&k->prev->node);

		k = k->prev;
		kmem_merge(k);
		INIT_LIST_HEAD(&k->node);
	}

	if (!k->next) {
		/* last item in heap memory, return to system */
		k->prev->next = NULL;
		_kmem_last = k->prev;

		/* release back */
		kernel_sbrk(-(k->size + sizeof(struct kmem)));
		rel = 1;

	} else {
		list_add_tail(&k->node, &_kmem_init->node);
	}

	if (rel)
		printf("SBRK: %p F: %d A: %d\n", kernel_sbrk(0), n_free, n_alloc);
#endif /* CONFIG_MMU */
}







void verify(void)
{
	uint32_t i;

	unsigned char *p = (unsigned char *) sbrk;


	return;

	for (i = 0; i < addr_hi - sbrk; i++) {
		if (p[i] != 0x55) {
			printf("error at addr %08x (offset %d), value %02x\n", sbrk + i,i,  p[i]);
			exit(-1);
		}
	}

}

#define P 350
#define LEN_BIG		7 * 1024 * 1024
#define LEN_SMALL	5 * 100


void run_test(void)
{
	int i, j;
	static uint32_t *p[P];
	uint32_t len[P];
	int v;


	while (1) {

		for (i = 0; i < P; i++) {
			if (p[i])
				continue;

			if (rand() % 10 == 0)
				len[i] = rand() % LEN_BIG;
			else
				len[i] = rand() % LEN_SMALL;

			p[i] = kmalloc(len[i]  * sizeof(uint32_t));
			if (!p[i])
				break;;

			for (j = 0; j < len[i]; j++)
				p[i][j] = 0xaaaaaa;
		}

		if (rand() % 100 == 0) {
			for (i = 0; i < P; i++) {
				kfree(p[i]);
				p[i] = NULL;
				verify();
			}
		}


		if (rand() % 10 != 0)
		{
			for (i = 0; i < P; i++) {

				v = rand();
				if (v % 3 != 0)
					continue;

				kfree(p[i]);
				p[i] = NULL;

				verify();
			}
		} else {
			for (i = 0; i < P; i++) {
				kfree(p[i]);
				p[i] = NULL;
				verify();
			}
		}
	}
}




int main(void)
{
	mem_base = calloc(MEMSIZE_MAX + 4 * PAGE_SIZE, 1);


	addr_lo = PAGE_ALIGN(((uint32_t) mem_base));

	addr_hi = addr_lo + MEMSIZE_MAX;

	sbrk = (uint32_t) addr_lo;

	memset((void *) addr_lo, 0x55, addr_hi-addr_lo);

	kmem_init();

	run_test();
}
