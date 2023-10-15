# Slub 设计文档

> Contributors：焦心雨(2112536)、李艺楠(2110246)、辛浩然(2112514)
>
> [GitHub链接](https://github.com/Herren-Hsing/Operation-System)

## 基本思想

### SLAB

`Buddy System`提供了以页为单位的内存分配接口，这对内核来说颗粒度还太大了，所以需要一种新的机制，将页拆分为更小的单位来管理。`slab`分配器就应运而生了。`slab`分配器分配内存以`Byte`为单位。但是`slab`分配器并没有脱离伙伴系统，而是基于伙伴系统分配的大内存进一步细分成小内存分配。

<img src="https://s2.loli.net/2023/10/14/Bojp71GSvfd2H9c.png" alt="image.png" style="zoom: 40%;" />

#### 基本结构

<img src="https://s2.loli.net/2023/10/14/BDHSc4gOjbeLlty.png" alt="image.png" style="zoom: 33%;" />

`cache_chain`是一个`kmem_cache`的链表，描述了一个高速缓存，每个高速缓存包含了一个`slabs`的列表，这通常是一段连续的内存块。存在3种`slab`：

- `slabs_full`(完全分配的`slab`)
- `slabs_partial`(部分分配的`slab`)
- `slabs_empty`(空`slab`，或者没有对象被分配)。

`slab`是`slab`分配器的最小单位，在实现上一个`slab`有一个或多个连续的物理页组成（通常只有一页）。单个`slab`可以在`slab`链表之间移动，例如如果一个半满`slab`被分配了对象后变满了，就要从`slabs_partial`中被删除，同时插入到`slabs_full`中去。

当应用程序请求分配内存时，`slab`分配器首先在`partial list`中查找是否存在适合大小的内存块。如果找到，则该内存块被分配并从`partial list`中移除。如果`partial list`中没有适合的内存块，则`slab`分配器会从`empty list`中获取一块内存，并将其划分为多个大小相同的块。其中一块被分配给请求方，并剩余的块被放入`partial list`中。

当释放内存时，被释放的内存块将被放回`partial list`中。如果`partial list`已满，则会将其中的一些块移动到`full list`中，以供下次分配使用。在`full list`中的块只能被释放到`empty list`中，以便它们可以被重新划分为多个小块以供下次使用。

### SLUB

`slub`是`slab`的增强版，它的出现是为了解决`slab`存在的一些问题。最主要的改变是，每个`node`节点有三个链表，分别记录空闲`slab`、部分空闲`slab`和非空闲`slab`。当回收操作来不及时，三个链表记录的页框会较长时间停留到`slab`管理器中，不利于提高内存的使用率。针对这点，`slub`只保留了一个链表，就是部分空闲链表。

基本结构如下图所示：

<img src="https://s2.loli.net/2023/10/14/YJRGN8K72M3s5Cx.png" alt="image.png" style="zoom: 50%;" />

`Slub`的标准流程为：

#### 分配

<img src="https://s2.loli.net/2023/10/14/G1X2zeMiUrS5kha.png" alt="image.png" style="zoom: 67%;" />

分配时，从`cpu_slab` 分配最快，能从`cpu_slab`当前`freelist` 分配就从`cpu_slab`当前`freelist `分配，不能则从`partial` 列表中把一个`slab page`拿出来放到`freelist`，如果还没有就从`node` 里找`slab page`，都没有再新申请`slab page`。

- 首先判断请求`kmalloc`分配内存的大小，大于`slab`可以分配的上限(通常8k)则调用`kmalloc_large`，根据大小调用伙伴系统分配适当的页数。
- 其他大小可以使用`slab`申请，则根据申请`flag` 和申请大小计算出`kmalloc_caches`的类型和`index`，获取对应的`slab`管理结构体。 
  - 如果`flag` 存在`ACCOUNT`相关可能要切换到计数后`slab`，即`kmalloc-cg`相关`slab`(`slab_pre_alloc_hook`) 
  - 获得`slab` 之后获取当前`cpu` 的`cpu_slab`：
    - 如果当前`cpu_slab` 的`freelist` 不为空，则直接将`freelist` 的第一个分配返回，然后`freelist`向后移动，更新`tid`等。 
    - 如果`cpu_slab` 的`freelist` 为空，则看`cpu_slab` 的`partial` 链表是否为空，不为空则将`partial` 链表第一个`slab page` 切换给`cpu_slab` `freelist`，然后分配，`partial`指向下一个`slab page`。 
    - 如果以上都失败，说明`cpu_slab`无法完成分配，需要新`slab`：
      - 则找到合适当前运行`cpu` 的内存`node`所属`kmem_cache_node`结构，查询`partial` 链表是否有可用`slab page`有的话将这个`slab page`给`cpu_slab` ，然后也从这个`node`取出一些`slab page`补充到`cpu_slab->partial`，然后从`freelist` 分配一个，跟上面一样。 
      - 如果还是没有，则调用`new_slab`申请一个全新`slab page`，从新`slab page`的`freelist` 分配。

<img src="https://s2.loli.net/2023/10/14/Fo4Ke18VAhQ9R3B.png" alt="image.png" style="zoom: 50%;" />

内存对象释放主要思路是：如果是页面对象，则直接伙伴系统释放，如果是`slab` 对象，如果在`cpu_slab`中，在`cpu_slab`中释放，如果不是，则根据释放之前之后的状态(为空、为满、半满)，进行不同的操作。

- 首先找到释放的内存对象所在的`page` 的`page`结构体。
  - 如果该`page`不是`slab`，也就是说内存对象不是通过`slab`分配的，而是直接分配的页(大块内存)，那么直接释放页。

- 大块内存分配的时候就是从伙伴系统直接分配的页，释放的时候页通过伙伴系统释放页面(`__free_pages`) 其他内存是通过`slab`分配，则通过`slab`释放(`slab_free`)
  - `memcg`相关处理(`memcg_slab_free_hook`)
  - 获取当前`cpu_slab`，如果要释放的内存对象正好属于当前`cpu_slab`(可以理解为是否是从当前`cpu_slab`分配的)，则快速释放
    - 获取`cpu_slab`的`freelist`，将该内存对象插入`freelist`头部，刷新`cpu_slab`相关信息(`do_slab_free`) 
  - 如果要释放的内存对象不属于当前`cpu_slab`，(当前`slab page`在`cpu_slab->partial`、别的`cpu_slab->page`、别的`cpu_slab->partial`、游离状态、`node->partial` 、`node->full`6种情况)，需要慢速释放(`__slab_free`)
    - 先把内存对象释放到`slab page`的`freelist`头部，更新`slab page`相关统计信息 
    - 如果该`slab page`为冻结状态(说明是在`cpu_slab`中的三种情况)
      -  则直接结束(已经将`object` 放到`page->freelist`了，剩下的就不用管了) 
    - 如果释放前该`slab page`是满的(`freelist`为空)，则说明`page`目前是游离状态(不在任何列表中)或`node->full`中 
      - 如果开启`cpu->partial`，则将该`slab page`放到`cpu_slab->partial`中(从`node->full`中移除) 
        - 如果`cpu_slab->partial`满了，则要将当前`cpu_slab->partial`中的所有`slab page`放到`node->partial`中，然后再将新的`slab page`放到`cpu_slab->partial`中 
      - 否则放入`node->partial`中(从`node->full`中移除) 
    - 如果释放后为空，则说明目前该`slab page`一定处在`node->partial`列表中，因为如果在`cpu_slab`或者游离状态或`node->full`中不管释放完是否为空，都会在上面的步骤中处理完毕
      -  如果当前`slab` 管理的`partial`页面数量满足最小要求，则将该释放后为空的`slab page`释放掉(`__free_pages`) 
      - 否则不变，继续呆在`node->partial`中 
    - 否则说明该`page`是本来就呆在`node->partial`中的半满`page`，并且释放后还是半满，则什么也不操作。 

## 算法实现

参考相关资料，实现了简易`Slub`算法：

- 我们在实现`cpu_slab`中遇到了困难，为了能够形成完整的页面分配，我们结合传统的`slab`页面管理，采用`slab`的数据结构，`node`节点维护三个链表。
- 简化处理：
  - 假设一个`slab`是一页；
  - 设定一系列能分配固定字节大小`obj`的`cache`。

<img src="https://s2.loli.net/2023/10/14/BDHSc4gOjbeLlty.png" alt="image.png" style="zoom: 33%;" />

### 数据结构

`slab cache`的描述符：`struct kmem_cache_t`，用于描述一种`slab cache`，并管理着一个或多个该`slab`中的的所有`obj`。其类成员包括`cache`的描述信息，比如名称、`obj`数目及大小，包括实现相关链接的`cache_link`。

此外，每个`cache`都对应一个节点，维护三个链表，分别是`slabs_partial`(部分空闲`slab`链表)、非空闲`slab`链表（`slabs_full`）和全空闲`slab`链表(`slabs_free`)。

```c
struct kmem_cache_t
{
    char name[CACHE_NAMELEN];
    list_entry_t slabs_full;
    list_entry_t slabs_partial;
    list_entry_t slabs_free;
    uint16_t objsize;
    uint16_t num;
    void (*ctor)(void *, struct kmem_cache_t *, size_t);
    void (*dtor)(void *, struct kmem_cache_t *, size_t);
    list_entry_t cache_link;
};
```

描述`slab`的描述符：`struct slab_t`。我们假设一个`slab`是一页。在这个结构体中维护：

- 引用次数`ref`；
- 指向`cache`的指针；
- 被使用的`obj`对象个数；
- 第一个可用`obj`的索引值；
- 链接`slab`的`slab_link`；
- 通过以上成员的设计，`slab_t`结构体与`Page`结构体占用内存相等。

```c
struct slab_t
{
    int ref;
    struct kmem_cache_t *cachep;
    uint16_t inuse;
    uint16_t free;
    list_entry_t slab_link;
};
```

`slab`的基本结构，我们实现的`slab`的基本结构包括两部分：可用`obj`的索引数组和分配的对象`obj`；每个索引存储的是下一个可用`obj`的索引。将最后一个索引的值设为-1，表示分配完所有的`obj`。

> 比如，在下面的图中，`slab`中的`free`成员的值可以设置为0，第一个可用`obj`为0号，当分配该`obj`后，`free`的值更新为`bufctl[slab->free]`，即`bufctl[0]==1`，第一个可用`obj`设置为刚才分配的`obj`的下一个`obj`。

<img src="https://s2.loli.net/2023/10/14/qmzRIYVGSEKvb2s.png" alt="image.png" style="zoom:50%;" />

### 初始化

初始化`cache`的链表`cache_chain`，首先创建`cache_cache`，它能够进行分配`cache`；

然后我们设定一些固定分配的`obj`大小的`cache`，接下来调用`kemm_cache_create`去创建这些`cache`。

```c
#define SIZED_CACHE \
    16, 32, 64, 128, 256, 512, 1024, 2048

#define SIZED_CACHE_NUM 8

static struct kmem_cache_t *sized_caches[SIZED_CACHE_NUM];

struct kmem_cache_t cache_cache = {
    .objsize = sizeof(struct kmem_cache_t),
    .num = PGSIZE / sizeof(struct kmem_cache_t),
    .ctor = NULL,
    .dtor = NULL,
    .name = "cache_cache"};

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
        sized_caches[i] = kmem_cache_create(sized_cache_name, sized_cache[i], NULL, NULL); // 创建指定大小的cache

    // 检查 kmem
    check_kmem();
}
```

`kemm_cache_create`创建这些`cache`，需要在`cache_cache`的列表中中分配`cache`，也就相当于在`cache`的列表中分配`obj`。因此调用`kmem_cache_alloc`函数。这个函数的实现在下一部分具体分析。

在分配`cache`之后，对它们进行初始化，初始化成员变量的值，将`cache`加入`cache_chain`，并初始化每个`cache`维护的三个链表。

```c
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

```

### 分配

分配对象的函数`kmem_cache_alloc`分配过程如下：

- 首先查看部分列表，如果部分列表不为空，获取部分列表头的下一个链表项；

- 如果部分列表为空，查看空闲列表。

  - 如果空闲列表不为空，获取列表头的下一个链表项；
  - 如果为空，调用`kmem_cache_grow`查看是否能够增加对象：
    - 如果不能返回`NULL`；

- 接下来分配对象，在相应链表里删除选取的链表项

- 然后需要找到分配对象的地址：

  - 分配对象的地址 =` cache`的地址偏移`cache->num`个单位（可用`obj`索引数组），再越过前面的不可用`obj`
  - `cache`的地址：
    - 通过`le2slab(le, page_link)`将链表项转为`slab`结构体
    - 调用`slab2kva(slab)`获取`slab`对应的页的实际虚拟地址
  - 偏移`cache->num`个单位，即一个`bufctl`数组（可用`obj`索引数组）
  - 越过前面的不可用`obj`：
    - 可用`obj`索引`slab->free`乘上`obj`大小`slab->objsize`

- 接下来，更新`slab`信息：

  - 使用数量`inuse`加一；
  - 更新可用`obj`索引，为刚才分配的`obj`的下一个可用`obj`索引

  - 如果`slab`全部分配，添加到`full`；否则添加到`partial`。

```c
// 分配一个对象
void * kmem_cache_alloc(struct kmem_cache_t *cache_p)
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
```

在上面的代码中，如果这个`cache`没有空闲`slab`，需要调用`kmem_cache_glow`函数增加一个`slab`。

- 分配一个内存页；
- 内存页结构体的地址即赋值为`slab`结构体的地址；
- 设置`slab`的`cache`指针，设置成员变量，加入到空闲`slab`表中；
- 初始化`slab`中的索引数组`bufctl`，设置空闲对象的索引，最后一个对象的索引设置为-1，表示没有更多的空闲对象。
- 如果有构造函数，初始化`obj`

```c
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
```

此外，还实现了两个相关分配函数：

`sized_cache_malloc`分配指定大小的内存块。因为我们只设定了分配固定大小`obj`的`cache`，所以如果指定字节大小不在设定值中，需要向上舍入，然后找到对应的`cache`索引，进行分配。

```c
// 分配指定大小的内存块
void *sized_cache_malloc(size_t size)
{
    return kmem_cache_alloc(sized_caches[get_sized_index(size)]);
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
```

`kmem_cache_zalloc`分配一个对象，并使用`memeset`将其全部填充为0.

```c
// 分配一个对象并将其填充为零
void *kmem_cache_zalloc(struct kmem_cache_t *cachep)
{
    void *objp = kmem_cache_alloc(cachep);

    memset(objp, 0, cachep->objsize);

    return objp;
}
```

### 释放

#### 释放`obj`

`kmem_cache_free`函数实现对对象的释放。

- 根据要释放的对象地址找到所在的`slab`结构体地址：

  - `obj`向下对齐到页大小，求出相对分页`base`的偏移页数

  - 因为`slab`是以页为单位划分的，可用根据页结构体数组找到管理该页的结构体地址，即`slab`地址。

- 获取该对象在`slab`中的序号，更新索引数组和相关成员变量。

- 将`slab`从链表中取下，如果所有`obj`均未分配，则加入到`free`，否则加入到`partial`链表。

```c
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
```

此外，还实现了`kfree`，来释放未指定`cache`的`obj`，这就需要找到它的`slab`，根据`slab`成员变量里指向`cache`的指针找到`cache`，然后调用前面的释放函数`kmem_cache_free`。

```c
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
```

#### 释放`slab`

释放一个`slab`：

- 如果存在析构函数，需要析构其所有的`obj`对象
- 可将`slab`结构转为`page`结构，重置`page`属性，将其从双向链表删除，释放该页。

```c
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
```

#### 释放`cache`

释放`cache`，销毁它三个链表中的所有`slab`。然后调用`kmem_slab_destroy`删除这个`cache`。

```c
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
```

#### 销毁所有自由`slab`

遍历`cache`链表，销毁所有`free`链表中的的`slab`，对于每个`cache`，遍历`free`链表，调用`kmem_slab_destroy`函数销毁`slab`

```c
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
```

## 算法测试

> 测试前，需要将`kern_init`函数中的`kmem_init();`取消注释。

编写测试函数，涉及到以上编写分配、释放、销毁、收缩缓存等函数的验证，并增设虚构函数和构造函数，验证对象是否正确析构和构造。

成功通过测试：

![image.png](https://s2.loli.net/2023/10/15/es7qCO9afWh1I8y.png)
