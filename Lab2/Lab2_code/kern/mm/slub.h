#ifndef __KERN_MM_SLUB_H__
#define __KERN_MM_SLUB_H__

#include <pmm.h>
#include <list.h>

#define CACHE_NAMELEN 16

struct slab_t
{
    int ref;
    struct kmem_cache_t *cachep;
    uint16_t inuse;
    uint16_t free;
    list_entry_t slab_link;
};

struct kmem_cache_t
{
    char name[CACHE_NAMELEN];
    list_entry_t slabs_full;
    list_entry_t slabs_partial;
    list_entry_t slabs_free;
    uint16_t objsize; // 对象的大小
    uint16_t num;     // 对象的数目
    void (*ctor)(void *, struct kmem_cache_t *, size_t);
    void (*dtor)(void *, struct kmem_cache_t *, size_t);
    list_entry_t cache_link;
};

void kmem_init();
struct kmem_cache_t *kmem_cache_create(const char *name, size_t size,
                                       void (*ctor)(void *, struct kmem_cache_t *, size_t),
                                       void (*dtor)(void *, struct kmem_cache_t *, size_t)); // 创建一个内存缓存
static void *kmem_cache_grow(struct kmem_cache_t *cache_p);                                  // 为给定的内存缓存增长并分配新的slab

void *sized_cache_malloc(size_t size);                // 用于从系统内存中分配指定大小的内存块
void *kmem_cache_alloc(struct kmem_cache_t *cachep);  // 从给定的内存缓存中分配一个内存块
void *kmem_cache_zalloc(struct kmem_cache_t *cachep); // 分配的内存块会被初始化为零

static int get_sized_index(size_t size);

void kfree(void *objp);
void kmem_cache_free(struct kmem_cache_t *cachep, void *objp);

static void kmem_slab_destroy(struct kmem_cache_t *cachep, struct slab_t *slab);
void kmem_cache_destroy(struct kmem_cache_t *cachep);

int kmem_cache_shrink(struct kmem_cache_t *cachep);

int kmem_cache_reap();

static void check_kmem();

#endif /* ! __KERN_MM_SLUB_H__ */