#include <slub.h>
#include <list.h>
#include <defs.h>
#include <string.h>
#include <stdio.h>

#define SIZED_CACHE \
    16, 32, 64, 128, 256, 512, 1024, 2048

#define SIZED_CACHE_NUM 8

#define ADD_TO_LIST(list, node) list_add(&(list), &(node))

#define le2slab(le, link) ((struct slab_t *)le2page((struct Page *)le, link))
#define slab2kva(slab) (page2kva((struct Page *)slab))

struct kmem_cache_t cache_cache = {
    .objsize = sizeof(struct kmem_cache_t),
    .num = PGSIZE / sizeof(struct kmem_cache_t),
    .ctor = NULL,
    .dtor = NULL,
    .name = "cache_cache"};

static list_entry_t cache_chain;
static struct kmem_cache_t *sized_caches[SIZED_CACHE_NUM];

void kmem_init()
{
    list_init(&(cache_cache.slabs_full));
    list_init(&(cache_cache.slabs_partial));
    list_init(&(cache_cache.slabs_free));
    list_init(&(cache_chain));
    ADD_TO_LIST(cache_chain, cache_cache.cache_link); // 将cache_cache添加到cache_chain链表中
    char sized_cache_name[] = "sized";

    int sized_cache[] = {SIZED_CACHE}; // 指定cache大小
    // 初始化sized cache
    for (int i = 0; i < SIZED_CACHE_NUM; i++)
        sized_caches[i] = kmem_cache_create(sized_cache_name, sized_cache[i], NULL, NULL); // 创建指定大小的缓存

    // 检查 kmem
    check_kmem();
}

// 创建 kmem_cache
struct kmem_cache_t *
kmem_cache_create(const char *name, size_t size,
                  void (*ctor)(void *, struct kmem_cache_t *, size_t),
                  void (*dtor)(void *, struct kmem_cache_t *, size_t))
{
    assert(size <= (PGSIZE - 2));
    struct kmem_cache_t *cache_p = kmem_cache_alloc(&(cache_cache));
    if (cache_p != NULL)
    {
        cache_p->objsize = size;
        cache_p->num = PGSIZE / (sizeof(int16_t) + size);
        cache_p->ctor = ctor;
        cache_p->dtor = dtor;
        memcpy(cache_p->name, name, CACHE_NAMELEN);
        list_init(&(cache_p->slabs_full));
        list_init(&(cache_p->slabs_partial));
        list_init(&(cache_p->slabs_free));
        ADD_TO_LIST(cache_chain, cache_p->cache_link);
    }
    return cache_p;
}

// 分配指定大小的内存块
void *sized_cache_malloc(size_t size)
{
    return kmem_cache_alloc(sized_caches[get_sized_index(size)]);
}

// 分配一个对象
void *
kmem_cache_alloc(struct kmem_cache_t *cache_p)
{
    list_entry_t *le = NULL; // 声明一个链表项指针，初始化为NULL

    // 在部分列表中查找
    if (!list_empty(&(cache_p->slabs_partial)))    // 如果部分列表不为空
        le = list_next(&(cache_p->slabs_partial)); // 获取下一个链表项

    // 在空闲列表中查找
    else
    {
        if (list_empty(&(cache_p->slabs_free)) && kmem_cache_grow(cache_p) == NULL)
            return NULL;                        // 如果空闲列表为空并且无法增长，则返回NULL
        le = list_next(&(cache_p->slabs_free)); // 获取下一个链表项
    }

    // 分配对象
    list_del(le); // 从链表中删除该链表项

    struct slab_t *slab = le2slab(le, page_link); // 将链表项转换为slab结构体
    int16_t *bufctl = slab2kva(slab);             // 获取slab虚拟地址，转换为int16_t指针
    void *buf = bufctl + cache_p->num;
    void *objp = buf + slab->free * cache_p->objsize;

    // 更新slab信息
    slab->inuse++;
    slab->free = bufctl[slab->free];

    if (slab->inuse == cache_p->num)
        list_add(&(cache_p->slabs_full), le); // 如果slab全部分配，则添加到满列表
    else
        list_add(&(cache_p->slabs_partial), le); // 否则添加到部分列表

    return objp; // 返回分配的对象的指针
}

// 增加一个空闲slab
static void *
kmem_cache_grow(struct kmem_cache_t *cache_p)
{
    struct Page *page = alloc_page();                  // 分配一页内存页
    struct slab_t *slab = (struct slab_t *)page;       // slab结构体管理这一页
    slab->cachep = cache_p;                            // 设置slab的缓存指针
    slab->inuse = slab->free = 0;                      // 初始化已使用和空闲对象计数
    ADD_TO_LIST(cache_p->slabs_free, slab->slab_link); // 将slab添加到缓存的空闲slab链表中

    // 初始化bufctl
    int16_t *bufctl = page2kva(page); // 获取页的虚拟地址
    for (int i = 1; i < cache_p->num; i++)
        bufctl[i - 1] = i;         // 初始化bufctl数组，设置空闲对象的索引
    bufctl[cache_p->num - 1] = -1; // 最后一个对象的索引设置为-1，表示没有更多的空闲对象

    // 初始化obj
    void *buf = bufctl + cache_p->num;
    if (cache_p->ctor)
        for (void *p = buf; p < buf + cache_p->objsize * cache_p->num; p += cache_p->objsize)
            cache_p->ctor(p, cache_p, cache_p->objsize); // 如果存在构造函数，初始化缓存中的对象

    return slab; // 返回分配的slab
}

// 分配一个对象并将其填充为零
void *kmem_cache_zalloc(struct kmem_cache_t *cachep)
{
    void *objp = kmem_cache_alloc(cachep);

    memset(objp, 0, cachep->objsize);

    return objp;
}

static int get_sized_index(size_t size)
{
    int sized_cache[] = {SIZED_CACHE};
    // 向上舍入到最接近的2的幂次方
    size_t rsize = ROUNDUP(size, 2);
    if (rsize < sized_cache[0])
        rsize = sized_cache[0];

    // 查找对应的索引
    int index = 0;
    for (int t = rsize / 32; t; t /= 2)
        index++;

    return index;
}

void kfree(void *objp)
{
    void *base = slab2kva(pages);

    // 将要释放的对象的地址向下对齐到页大小
    void *kva = ROUNDDOWN(objp, PGSIZE);

    // 通过地址计算出slab的指针
    struct slab_t *slab = (struct slab_t *)&pages[(kva - base) / PGSIZE];

    // 调用kmem_cache_free函数释放对象
    kmem_cache_free(slab->cachep, objp);
}

// 释放对象
void kmem_cache_free(struct kmem_cache_t *cachep, void *objp)
{
    void *base = page2kva(pages);
    // 将要释放的对象的地址向下对齐到页大小
    void *kva = ROUNDDOWN(objp, PGSIZE);
    // 通过地址计算出slab的指针
    struct slab_t *slab = (struct slab_t *)&pages[(kva - base) / PGSIZE];
    // 获取在slab中的偏移

    int16_t *bufctl = kva;
    void *buf = bufctl + cachep->num;
    int offset = (objp - buf) / cachep->objsize;

    // 更新slab
    list_del(&(slab->slab_link));
    bufctl[offset] = slab->free;
    slab->inuse--;
    slab->free = offset;

    if (slab->inuse == 0)
        ADD_TO_LIST(cachep->slabs_free, slab->slab_link);
    else
        ADD_TO_LIST(cachep->slabs_partial, slab->slab_link);
}

// 销毁一个slab
static void
kmem_slab_destroy(struct kmem_cache_t *cachep, struct slab_t *slab)
{
    // 将slab结构转换为Page结构
    struct Page *page = (struct Page *)slab;

    // 获取slab中的bufctl数组
    int16_t *bufctl = page2kva(page);

    // 计算buf的起始地址
    void *buf = bufctl + cachep->num;

    // 如果存在析构函数（dtor），则依次调用析构函数销毁slab中的对象
    if (cachep->dtor)
    {
        for (void *p = buf; p < buf + cachep->objsize * cachep->num; p += cachep->objsize)
        {
            cachep->dtor(p, cachep, cachep->objsize);
        }
    }

    // 重置Page的属性，将其标志位清零，从双向链表中移除
    page->property = page->flags = 0;
    list_del(&(page->page_link));

    // 释放slab占用的页面
    free_page(page);
}

// 销毁一个kmem_cache
void kmem_cache_destroy(struct kmem_cache_t *cachep)
{
    list_entry_t *head, *le;

    // 销毁满的slabs
    head = &(cachep->slabs_full);
    le = list_next(head);
    while (le != head)
    {
        list_entry_t *temp = le;
        le = list_next(le);

        // 调用kmem_slab_destroy函数销毁slab
        kmem_slab_destroy(cachep, le2slab(temp, page_link));
    }

    // 销毁部分满的slabs
    head = &(cachep->slabs_partial);
    le = list_next(head);
    while (le != head)
    {
        list_entry_t *temp = le;
        le = list_next(le);

        // 调用kmem_slab_destroy函数销毁slab
        kmem_slab_destroy(cachep, le2slab(temp, page_link));
    }

    // 销毁空闲的slabs
    head = &(cachep->slabs_free);
    le = list_next(head);
    while (le != head)
    {
        list_entry_t *temp = le;
        le = list_next(le);

        // 调用kmem_slab_destroy函数销毁slab
        kmem_slab_destroy(cachep, le2slab(temp, page_link));
    }

    // 释放kmem_cache对象
    kmem_cache_free(&(cache_cache), cachep);
}

// 销毁自由列表中的所有slabs
int kmem_cache_shrink(struct kmem_cache_t *cachep)
{
    int count = 0;

    // 遍历自由slabs列表
    list_entry_t *le = list_next(&(cachep->slabs_free));
    while (le != &(cachep->slabs_free))
    {
        list_entry_t *temp = le;
        le = list_next(le);

        // 调用kmem_slab_destroy函数销毁slab
        kmem_slab_destroy(cachep, le2slab(temp, page_link));
        count++;
    }

    return count;
}

// 收回所有自由的slabs
int kmem_cache_reap()
{
    int count = 0;

    // 遍历缓存链表
    list_entry_t *le = &(cache_chain);
    while ((le = list_next(le)) != &(cache_chain))

        // 调用kmem_cache_shrink函数销毁自由slabs，并统计销毁的数量
        count += kmem_cache_shrink(to_struct(le, struct kmem_cache_t, cache_link));

    return count;
}


/*         begin test        */

#define TEST_OBJECT_LENTH 2046
#define TEST_OBJECT_CTVAL 0x22
#define TEST_OBJECT_DTVAL 0x11

static const char *test_object_name = "test";

// 定义一个包含字符数组成员的测试结构体
struct test_object
{
    char test_member[TEST_OBJECT_LENTH];
};

// 初始化测试对象时使用的构造函数
static void
test_ctor(void *objp, struct kmem_cache_t *cachep, size_t size)
{
    char *p = objp;
    for (int i = 0; i < size; i++)
        p[i] = TEST_OBJECT_CTVAL;
}

// 销毁测试对象时使用的析构函数
static void
test_dtor(void *objp, struct kmem_cache_t *cachep, size_t size)
{
    char *p = objp;
    for (int i = 0; i < size; i++)
        p[i] = TEST_OBJECT_DTVAL;
}

// 计算一个双向循环链表的长度
static size_t
list_length(list_entry_t *listelm)
{
    size_t len = 0;
    list_entry_t *le = listelm;
    while ((le = list_next(le)) != listelm)
        len++;
    return len;
}

static void check_kmem()
{

    // 检查两个结构体的大小是否相等
    assert(sizeof(struct Page) == sizeof(struct slab_t));

    // 获取系统中的空闲页面数
    size_t fp = nr_free_pages();

    // 创建一个内存缓存，用于分配测试对象
    struct kmem_cache_t *cp0 = kmem_cache_create(test_object_name, sizeof(struct test_object), test_ctor, test_dtor);
    assert(cp0 != NULL);

    // 分配六个测试对象
    struct test_object *p0, *p1, *p2, *p3, *p4, *p5;
    char *p;
    assert((p0 = kmem_cache_alloc(cp0)) != NULL);
    assert((p1 = kmem_cache_alloc(cp0)) != NULL);
    assert((p2 = kmem_cache_alloc(cp0)) != NULL);
    assert((p3 = kmem_cache_alloc(cp0)) != NULL);
    assert((p4 = kmem_cache_alloc(cp0)) != NULL);

    // 验证分配的对象是否被正确初始化
    p = (char *)p4;
    for (int i = 0; i < sizeof(struct test_object); i++)
        assert(p[i] == TEST_OBJECT_CTVAL);

    // 分配一个对象并验证其初始化为0
    assert((p5 = kmem_cache_zalloc(cp0)) != NULL);
    p = (char *)p5;
    for (int i = 0; i < sizeof(struct test_object); i++)
        assert(p[i] == 0);

    // 检查空闲页面数是否减少了3个
    assert(nr_free_pages() + 3 == fp);

    // 验证缓存的各个链表是否符合预期
    assert(list_empty(&(cp0->slabs_free)));
    assert(list_empty(&(cp0->slabs_partial)));
    assert(list_length(&(cp0->slabs_full)) == 3);

    // 释放三个对象
    kmem_cache_free(cp0, p3);
    kmem_cache_free(cp0, p4);
    kmem_cache_free(cp0, p5);

    // 验证链表状态
    assert(list_length(&(cp0->slabs_free)) == 1);
    assert(list_length(&(cp0->slabs_partial)) == 1);
    assert(list_length(&(cp0->slabs_full)) == 1);

    // 收缩缓存
    assert(kmem_cache_shrink(cp0) == 1);
    assert(nr_free_pages() + 2 == fp);

    // 验证对象是否被正确析构
    p = (char *)p4;
    for (int i = 0; i < sizeof(struct test_object); i++)
        assert(p[i] == TEST_OBJECT_DTVAL);

    // 释放剩余的对象
    kmem_cache_free(cp0, p0);
    kmem_cache_free(cp0, p1);
    kmem_cache_free(cp0, p2);

    // 验证内存回收功能
    assert(kmem_cache_reap() == 2);
    assert(nr_free_pages() == fp);

    // 销毁缓存
    kmem_cache_destroy(cp0);

    // 使用sized_cache_malloc分配指定大小内存并验证
    assert((p0 = sized_cache_malloc(2048)) != NULL);
    assert(nr_free_pages() + 1 == fp);
    kfree(p0);

    // 再次验证内存回收
    assert(kmem_cache_reap() == 1);
    assert(nr_free_pages() == fp);

    // 输出测试通过消息
    cprintf("check_kmem() succeeded!\n");
}
