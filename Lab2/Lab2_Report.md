# OS Lab 2

> Contributors：焦心雨(2112536)、李艺楠(2110246)、辛浩然(2112514)
>
> - 包括练习1&2及Challenge 3的内容；
>
> - Challenge 2 (Buddy System) 和 Challenge 3 (Slub) 分别在单独的 Design_Doc 中。
>
> [GitHub链接](https://github.com/Herren-Hsing/Operation-System)

## 练习 1 ：理解first-fit 连续物理内存分配算法

> `first-fit `连续物理内存分配算法作为物理内存分配一个很基础的方法，需要同学们理解它的实现过程。请大家仔细阅读实验手册的教程并结合`kern/mm/default_pmm.c`中的相关代码，认真分析`default_init`，`default_init_memmap`，`default_alloc_pages`， `default_free_pages`等相关函数，并描述程序在进行物理内存分配的过程以及各个函数的作用。 请在实验报告中简要说明设计实现过程，并回答first fit算法是否有进一步的改进空间？

物理内存分配的整体流程为：

`kern_entry`函数的主要任务是设置虚拟内存管理，将三级页表的物理地址和`Sv39`模式位写入`satp`寄存器，以建立内核的虚拟内存空间，为之后建立分页机制的过程做一个准备。具体实现过程为：

- 分配页表所在空间并初始化页表

  - 分配`4KiB`内存给预设的三级页表(设定 12 位对齐)
  - 前511个页表项均设置为0
  - 最后一个页表项：`PPN=0x8000`，标志位`VRWXAD`均为1(说明这不是一个页目录表项)

- 设置好页基址寄存器

  - 三级页表的虚拟地址取高位，减去虚实映射偏移量→物理地址→右移12位，得到物理页号→附加到`satp`的`PPN`字段

  - 设置`MODE`字段为`SV39`

- 刷新`TLB`

完成上述工作后，调用`kern_init`函数。`kern_init`函数在完成一些输出等工作后，将进入物理内存管理初始化的工作，即调用`pmm_init`函数完成物理内存的管理。接着是执行中断和异常相关的初始化工作，即调用`idt_init`函数。

为了完成物理内存管理，首先需要探测可用的物理内存资源；了解到物理内存位于什么地方，有多大之后，就以固定页面大小来划分整个物理内存空间，并准备以此为最小内存分配单位来管理整个物理内存，管理在内核运行过程中每页内存，设定其可用状态；接着`ucore kernel`就要建立页表，启动分页机制，让CPU的`MMU`把预先建立好的页表中的页表项读入到`TLB`中，根据页表项描述的虚拟页(`Page`)与物理页帧(`Page Frame`)的对应关系完成CPU对内存的读、写和执行操作。

接下来，具体描述物理内存管理过程，即分析`pmm_init`函数的实现。主要工作如下：

- 调用`init_pmm_manager`：定义一个物理内存管理器，用来分配和释放物理内存；
- 调用`page_init`：检测物理内存大小，保留已经使用的内存，然后创建一个空闲页面列表；
- 调用`check_alloc_page`：检查`pmm`中分配和释放函数的正确性；
- 输出`satp`虚拟地址和物理地址。

### 定义物理内存管理器

在`init_pmm_manager`内部，首先把物理内存管理器`pmm_manager`的指针赋值成`&default_pmm_manager`， 这是`pmm_manager`结构体的一个实例，该实例在分配页面时使用`First Fit`算法。所谓`First Fit`算法就是当需要分配页面时，它会从空闲页块链表中找到第一个适合大小的空闲页块，然后进行分配。当释放页面时，它会将释放的页面添加回链表，并在必要时合并相邻的空闲页块，以最大限度地减少内存碎片。

`pmm_manager`提供了各种接口：分配页面，释放页面，查看当前空闲页面数，那些接口只是作为函数指针，作为`pmm_manager`的一部分。接下来，把把那些函数指针变量赋值为实现的相应函数名称。

指定物理内存管理器后，调用`default_init`函数开始初始化。该函数的主要工作是在初始化一个管理空闲内存块的双向链表`free_list`，并把记录当前空闲页的个数的无符号整型变量`nr_free`置为0。当我们调用` list_init(&free_list)`时，就声明一个名为`free_list` 的链表头，它的 `next`、`prev` 指针都初始化为指向自己，这样，我们就有了一个表示空闲内存块链的空链表。

```c
static void
default_init(void) {
    list_init(&free_list);
    nr_free = 0;
}
```

```c
static inline void
list_init(list_entry_t *elm) {
    elm->prev = elm->next = elm;
}
```

### 初始化页面布局

为了管理物理内存，在前面定义了`Page`结构体，来存储当前使用了哪些物理页面，哪些物理页面没被使用这样的信息。我们需要将一些`Page`结构体在内存里排列在内核后面，这要占用一些内存。而摆放这些`Page`结构体的物理页面，以及内核占用的物理页面，之后都无法再使用了。

因此，我们用`page_init()`函数来确定除去内核和`page`结构体占用的物理内存外可用的物理内存范围，以便后续的内存分配和管理操作。

完成上述工作后，我们调用了函数`init_memmap()`，也就是`default_init_memmap()`。

`default_init_memmap()`根据现有的内存情况构建空闲块列表的初始状态，需要根据 `page_init `函数中传递过来的参数(某个连续地址的空闲块的起始页地址和页个数)来建立一个按照地址从小到大的顺序连接的的连续内存空闲块的双向链表。链表头是`free_list`，链表项是 `Page `数据结构的` base->page_link`。这样我们就依靠` Page `数据结构中的成员变量` page_link `形成了连续内存空闲块列表。

具体实现流程为：

- 判断`n`是否大于0，如果小于零终止；
- 初始化`n`个物理页，先判断每个页是不是保留页，如果是则终止；
  - 因为相邻编号的页对应的`Page`结构体在内存上是相邻的，所以可将第一个空闲物理页对应的`Page`结构地址作为基址，以基址+偏移量的方式访问所有空闲物理页的`Page`结构；
- 将这些页的标志位清`0`，清除引用此页的虚拟页个数；
- 将`base`的连续空页个数置为`n`，将`base`页面标记为空闲内存块的首个页面，计算空闲页总数；
- 将`n`个空闲页的首页`base`加入空闲链表：
  - 如果空闲链表为空，直接将`base`添加到链表中；
  - 否则，遍历空闲列表，将`base`页地址，比较每个链表节点对应的页结构体的地址，在合适的位置将`base`插入空闲链表，确保空闲链表按照地址从小到大的顺序排序；如果已经到达链表末尾，则将`base`添加到末尾。

```c
// default_pmm.c
// 参数：某个连续地址的空闲块的起始页，页个数
static void
default_init_memmap(struct Page *base, size_t n) {
    assert(n > 0); // 判断n是否大于0
    // assert的作用是先计算表达式 expression ，如果其值为假(即为0)，那么它先向stderr打印一条出错信息，然后通过调用 abort 来终止程序运行。
    struct Page *p = base; // 初始化n个连续物理页
    for (; p != base + n; p ++) {
        assert(PageReserved(p)); // 检查此页是否为保留页
        p->flags = p->property = 0; // 标志位清0
        set_page_ref(p, 0); // 清除引用此页的虚拟页个数，表示此物理页没有被虚拟页引用
    }
    base->property = n;  // 修改base的连续空页值为n
    SetPageProperty(base);  // 将base页面标记为空闲内存块的首个页面
    nr_free += n; // 计算空闲总页数

    if (list_empty(&free_list)) {
        list_add(&free_list, &(base->page_link)); 
        // 如果空闲链表为空，直接将base添加到链表中
    } else {
        list_entry_t* le = &free_list;
        while ((le = list_next(le)) != &free_list) { // 遍历空闲链表
            struct Page* page = le2page(le, page_link); 
            // 将链表节点转换为物理页结构，这里调用了le2page函数
            if (base < page) {
                list_add_before(le, &(base->page_link)); 
                // 将base插入空闲链表，确保按地址从小到大的顺序
                break;
            } else if (list_next(le) == &free_list) {
                list_add(le, &(base->page_link)); 
                // 如果已经到达链表末尾，则将base添加到末尾
            }
        }
    }
}
```

在前面的实现过程中，在遍历空闲链表时，需要访问到链表节点所在的宿主数据结构`page`结构体。这里使用了`le2page`宏定义，为`le2page(le, member) `：

- `le`，是指向数据结构 `page` 中 `list_entry_t `成员变量的指针，也就是存储在双向循环链表中的节点地址值；
- `member `则是` page `数据类型中包含的链表节点的成员变量，即`page_link`。

它的实现又用到了` to_struct `宏和` offsetof` 宏。首先先利用`offsetof`宏求得成员变量`member` 相对于结构体类型`page`起始位置的偏移量，这是一个不变的偏移量，然后存储在双向循环链表中的节点地址值减去偏移量就得到链表节点对应的宿主`page`结构体的地址。

```c
// convert list entry to page
#define le2page(le, member)                 \
to_struct((le), struct Page, member)

/* 返回 'member' 相对于结构体类型起始位置的偏移量 */
#define offsetof(type, member)                                      \
    ((size_t)(&((type *)0)->member))

/* *
 * to_struct - 从指针获取结构体
 * @ptr:    一个指向成员的结构体指针
 * @type:   嵌套在其中的结构体的类型
 * @member: 结构体内的成员的名称
 * */
#define to_struct(ptr, type, member)                               \
    ((type *)((char *)(ptr) - offsetof(type, member)))
```

### 验证分配、释放页面函数

接下来，调用`check_alloc_page`，来验证`pmm`中分配和释放函数的正确性。它通过分配、释放页面和检查页面属性，以及验证空闲链表的状态，来确保内存管理系统的正确性和稳定性，同时使用断言来验证各个条件是否满足预期。

因此，我们分析一下相应的分配和释放页面函数。

#### 分配空闲页

分配页面时，首先保存了当前的中断状态，禁用了中断，以避免中断干扰释放操作的原子性。然后，调用内存管理器的 `alloc_pages` 方法来实际执行页面分配，最后，恢复之前保存的中断状态，允许中断再次发生。

```c
struct Page *alloc_pages(size_t n) {
    struct Page *page = NULL;
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        page = pmm_manager->alloc_pages(n);
    }
    local_intr_restore(intr_flag);
    return page;
}
```

 `default_alloc_pages`的基本思想是：

- 在查找阶段，代码按照地址从小到大的顺序遍历系统中的空闲页框链表(`free_list`)，寻找那些连续的、空闲页面数量大于或等于所需数量 `n` 的页块。一旦找到这样的页块，它将被标记为待分配的页面，以表示找到了可供使用的页面。

- 如果查找成功，即找到了适合的页面块，会执行分配操作。这包括以下步骤：

  - 找到被分配页面块前面的页块，以确保后续插入操作的正确性。

  - 从空闲页框链表中移除当前被分配的页面块，表示该页面块已被分配出去。
  - 页面块分配了`n`个页，如果分配完了，还有连续的空闲空间，需要把空闲部分分割，插入空闲链表。

```c
static struct Page *
default_alloc_pages(size_t n) {
    assert(n > 0); // 判断n是否大于0
    if (n > nr_free) {  // 需要分配的页的个数大于空闲页的总数，直接返回
        return NULL;
    }
    struct Page *page = NULL;
    list_entry_t *le = &free_list; // 定义空闲链表的头部
    while ((le = list_next(le)) != &free_list) {  // 遍历整个空闲链表
        struct Page *p = le2page(le, page_link); // 转换为页结构
        if (p->property >= n) { // 找到合适的空闲页块即连续的空闲页数量大于n，可以分配
            page = p;
            break;
        }
    }
    if (page != NULL) { // 如果分配成功
        list_entry_t* prev = list_prev(&(page->page_link)); // 找到前一个页块
        list_del(&(page->page_link)); // 从free_list中删除当前页块
        if (page->property > n) {  // 如果连续页的数量大于所需要的大小，分割页
            struct Page *p = page + n;
            p->property = page->property - n; // 页的连续空间减小
            SetPageProperty(p); // 将 page+n 后的页面标记为空闲内存块的首个页面
            list_add(prev, &(p->page_link)); // 插入到原来页的位置
        }
        nr_free -= n; // 减去已经分配的页
        ClearPageProperty(page); // 清除分配空闲页首页的首个空闲页面标志
    }
    return page;
}
```

分配空闲页的具体流程：

- 首先判断空闲页的总数小是否大于所需的页块大小。 如果需要分配的页面数量`n`，已经大于了空闲页的数量，那么直接`return NULL`分配失败。 
- 遍历整个空闲链表：如果找到合适的空闲页块，即`p->property >= n`(该页块连续的空闲页数量大于n)，即可认为可分配。
  - 从空闲链表里删除该页，将总空闲页数减去分配的页数；调用`ClearPageProperty(p)`，对分配的首页重新设置标志位。
  - 如果当前空闲页的大小大于所需大小，则分割页块。具体操作就是，刚刚分配了`n`个页，如果分配完了，还有连续的空间，则在最后分配的那个页的下一个页(未分配)，调用`SetPageProperty(p)`，标记为连续空闲内存块的首个页面，插入到分配前的位置，更新它的连续空闲页值。

#### 释放页

释放页面时，首先保存了当前的中断状态，禁用了中断，以避免中断干扰释放操作的原子性。然后，调用内存管理器的 `free_pages` 方法来实际执行页面释放，最后，恢复之前保存的中断状态，允许中断再次发生。

```c
// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n) {
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        pmm_manager->free_pages(base, n);
    }
    local_intr_restore(intr_flag);
}
```

`default_free_pages`的实现基本思想是按顺序遍历一系列连续的要释放的物理页，判断是否能被释放，如果可以，则进行相应状态修改。然后，这些页被添加到空闲链表中，插入时保证按照地址顺序，并尝试与前后相邻的空闲块合并。

```c
static void
default_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    struct Page *p = base;
    for (; p != base + n; p ++) { 
    // 从base开始遍历检查(根据物理地址的连续性)需要释放的页是否被分配，如果被分配就把flags置为0，说明已经被分配，可以被释放，把引用次数改为0；
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = 0;
        set_page_ref(p, 0);
    }
    base->property = n; // 设置连续空页大小为n
    SetPageProperty(base);
    nr_free += n; // 空闲链表数量加n

    if (list_empty(&free_list)) { // 如果空闲链表为空，直接加入base
        list_add(&free_list, &(base->page_link));
    } else {
        list_entry_t* le = &free_list; // 空闲链表头
        while ((le = list_next(le)) != &free_list) {
            struct Page* page = le2page(le, page_link); // 转换为页结构
            // 还是要确保地址从小到大的顺序
            if (base < page) { 
                list_add_before(le, &(base->page_link));
                break;
            } else if (list_next(le) == &free_list) {
                list_add(le, &(base->page_link));
            }
        }
    }
    // 获取base的前一页块
    list_entry_t* le = list_prev(&(base->page_link));
    if (le != &free_list) { // 如果不是空闲链表的头部
        p = le2page(le, page_link); // 转化为页结构
        if (p + p->property == base) { // 如果这个页块和base是地址相邻的
            p->property += base->property;
            ClearPageProperty(base); // 清除base的首空闲页标记
            list_del(&(base->page_link)); // 从空闲链表里删除base
            base = p; // 新的base
        }
    }
    // 获取base的后一页块
    le = list_next(&(base->page_link));
    if (le != &free_list) { // 如果不是空闲链表的头部
        p = le2page(le, page_link); // 转化成页结构
        if (base + base->property == p){  // 如果base+base的连续的空闲大小之后等于当前页
            base->property += p->property; // 把p合并到base
            ClearPageProperty(p);
            list_del(&(p->page_link)); // 删除p，因为空闲链表链接的是空闲页块的首页
        }
    }
}
```

1. 首先确保传入的页面数量 `n` 大于` 0`，否则则终止。
2. 接下来，从`base`开始遍历检查需要释放的页是否被分配，如果被分配就把`flags`置为0，说明已经被分配，可以被释放，把引用次数改为`0`；
3. 将 `base` 页面的 `property` 字段设置为 `n`，表示这一连串页面的大小。
4. 使用 `SetPageProperty` 函数将 `base` 页面标记为首空闲页。
5. 将全局变量 `nr_free` 增加 `n`，表示可用空闲页面数量增加了 `n` 个。
6. 接下来，代码检查空闲链表 `free_list` 是否为空。如果为空，直接将 `base` 添加到空闲链表中。否则，需要按照地址从小到大的顺序将 `base` 插入到适当的位置。
   - 如果找到一个比 `base` 大的页块，将 `base` 插入到该页块之前。
   - 如果遍历到链表末尾仍未找到合适的位置，则将 `base` 添加到链表末尾。
7. 继续遍历链表，查找 `base` 的前一页块。如果前一页块与`base`地址相邻，那么将它与 `base` 合并，清除`base`的首空闲页标记，从空闲链表里删除`base`，并将合并后的页面作为新的 `base`。
8. 类似地，代码查找 `base` 的后一页块，如果它们相邻，将它们合并，清除后一页块首页的首空闲页标记并将其从链表中删除。

### 改进

分析以上`first fit`算法的代码，可以看到无论是`alloc`过程还是`free`过程都需要`O(n)`的复杂度。如果使用`first fit`算法，以上代码效率低的主要原因在于使用双向链表组织所有的`block`，这导致访问必须耗费线性时间。

考虑针对时间效率的优化方式如下：

- 采用`splay`等平衡二叉树结构来取代简单的链表结构来维护空闲块，其中按照中序遍历得到的空闲块序列的物理地址恰好按照从小到大排序；
- 每个二叉树节点上维护该节点为根的子树上的最大的空闲块的大小；
- 在每次进行查询的时候，不妨从根节点开始，查询左子树的最大空闲块是否符合要求，如果是的话进入左子树进行进一步查询，否则进入右子树；(二分查找)
- 按照上述方法，最终可以查找到物理地址最小的能够满足条件的空闲地址块，将其`splay`到平衡树的根，然后进行删除以及新的分裂出来的空闲块的插入等操作；
- 按照上述方法的话，每次查询符合条件的第一块物理空闲块的时间复杂度为`O(log n)`，对比原先的`O(n)`有了较大的改进；

![img](https:////upload-images.jianshu.io/upload_images/8878550-f892bf94ff27ceee.png?imageMogr2/auto-orient/strip|imageView2/2/w/747/format/webp)

>  `(xi,leni)`表示(起始地址，块大小)。其中`x0<x1<...<x6`，且块地址不相互重叠

## 练习2：实现 Best-Fit 连续物理内存分配算法(需要编程)

在完成练习一后，参考`kern/mm/default_pmm.c`对First Fit算法的实现，编程实现Best Fit页面分配算法，算法的时空复杂度不做要求，能通过测试即可。 

请在实验报告中简要说明你的设计实现过程，阐述代码是如何对物理内存进行分配和释放，并回答如下问题：

你的 Best-Fit 算法是否有进一步的改进空间？

### (一)Best-Fit 编程

Best_fit 的实现与 First _fit相类似。主要分为以下流程：

#### 1.初始化

`best_fit_init()`函数初始化一个物理内存管理器，将空闲内存块列表` free_list` 并将空闲内存块数量` nr_free` 设置为0。

#### 2.初始化内存映射

```C++
init_memmap(pa2page(mem_begin), (mem_end - mem_begin) / PGSIZE);
```

`best_fit_init_memmap()`用于根据现有的内存情况构建空闲块列表的初始状态。

##### (1)初始化空闲块

该函数接受两个参数：`struct Page *base`是要进行初始化的内存块的起始页首地址；`size_t n`是物理页帧的数量。

首先使用断言确保要初始化的内存块至少有一个块，如果不满足该条件那么该函数会先向stderr打印一条出错信息，然后通过调用` abort `来终止程序运行，后续操作将不被执行。

```CQL
assert(n > 0);
```

接下来进行内存块的初始化操作。对于每一个物理页帧，在初始化时都被设置为内核保留状态(`Page_init()`在`pmm.c`中)，因此我们先使用断言检查这些物理页帧是否是内核保留状态即是否完成`page_init()`。

```c
    struct Page *p = base;
    for (; p != base + n; p ++) {
        assert(PageReserved(p));
        /*LAB2 EXERCISE 2:   YOUR CODE */ 
        // 清空当前页框的标志和属性信息，并将页框的引用计数设置为0
        p->flags = p->property = 0;
        set_page_ref(p, 0);
    }
```

根据LAB2 EXERCISE 2的题目要求，我们需要清空当前页框的标志和属性信息，并将页框的引用计数设置为0。

对于每一个页，(`Page`定义在`memlayout.h`中)，页帧的标记为`flags`，目前用到了两个`bit`分别表示`PG_reserved`是否被内核保留、`PG_property`是否是一个空闲内存块的首个页面。

页帧的属性为`property`，用于记录某连续空闲页的数量。如果该页是空闲的，并且不是空闲块的第一页，则 `p->property `应设置为 0；如果此页是空闲的并且是空闲块的第一页，则` p->property` 应设置为连续内存空闲块的大小，即地址连续的空闲页的个数。

将标志和属性都设置为0，使用以下语句：

```C++
p->flags = p->property = 0;
```

页框的引用计数为`ref`，表示这个页被页表的引用的次数。在初始化时，该页面没有被映射过，因此使用以下指令使用设置为0。

```
set_page_ref(p, 0);
```

接下来需要对这个空闲块的第一页进行初始化设置：

```C++
base->property = n;//base页是空闲的并且是空闲块的第一页,base->property设置为连续内存空闲块的大小
SetPageProperty(base);// 将base页面标记为空闲内存块的首个页面
nr_free += n;//增加系统中空闲页的数量nr_free
```

完成初始化内存块后，我们需要将这个内存块插入到`free_list`中进行管理。

##### (2)将初始化的内存块插入空闲列表

将当前内存块按地址升序排列插入到`free_list`中时分为三种情况，插入到表头(`list_empty(&free_list)`)，表中和表尾(`list_next(le) == &free_list`)。

表头比较简单，如果当前free_list为空，那么就将这个内存块直接插入到链表中。

如果free_list不为空，那么就需要遍历每一个Page找到合适的位置。使用一个list_entry_t指针从第一个页面开始遍历，使用le2page将链表条目转化为Page。依次比较每一个Page与Base的地址，如果找到了第一个大于Base的地址的Page，就将base插入到它前面，并且跳出循环。

如果遍历所有的page后都没有找到大于base的地址，那么就将base插入到表尾，即最后一个页面与连续内存块首页的中间。

```C++
if (list_empty(&free_list)) {
        //如果为空，说明这是系统中第一个被初始化的内存块，直接将它添加到链表中
        list_add(&free_list, &(base->page_link));
    } else {
        list_entry_t* le = &free_list;//初始化为指向空闲列表的头部
        while ((le = list_next(le)) != &free_list) {//遍历列表中的页面块
            struct Page* page = le2page(le, page_link);//将链表条目转化为Page
            if (base < page) {//第一个大于Base的地址的Page
                list_add_before(le, &(base->page_link));//将base插入到它前面
                break;//跳出循环
            }
            else if (list_next(le) == &free_list) {//已经到达链表结尾
                list_add_before(le, &(base->page_link));//将base插入到链表尾部
                }
        }
    }
}
```

#### 3.内存分配

`best_fit_alloc_pages(size_t n)`函数用于分配内存，参数`size_t n`是需要分配的物理页的数量。

首先先检查分配请求是否合理。使用断言确保需要分配的物理页是大于0的数，并且判断当前的空闲页面数量是否能够支持分配。如果不满足分配条件就不执行分配。

```c++
    assert(n > 0);
    if (n > nr_free) {
        return NULL;
    }
```

在`best_fit`分配算法中，被分配的页面应当是满足分配需求的最小内存块。因此我们需要设定一个`min_size`变量，在遍历空闲列表的过程中找到最小的内存块。

```C
struct Page *page = NULL; // 分配的内存块地址
list_entry_t *le = &free_list; // 空闲列表的头部
size_t min_size = nr_free + 1; // 维护最小大小的变量
/*LAB2 EXERCISE 2:   YOUR CODE */  
// 下面的代码是first-fit的部分代码，请修改下面的代码改为best-fit
// 遍历空闲链表，查找满足需求的空闲页框
// 如果找到满足需求的页面，记录该页面以及当前找到的最小连续空闲页框数量
while ((le = list_next(le)) != &free_list) { // 遍历空闲列表
    struct Page *p = le2page(le, page_link); // 链表条目转化为Page结构体
    if(p->property >= n && p->property < min_size) { // 大于需要分配的物理页n且目前最小空闲内存块
        min_size = p->property; // 更新变量
        page = p; // 更新返回的指针
    }
} 
```

如果成功分配页面之后，就需要从空闲列表中删除这个页面。如果内存块的大小在分配后仍有剩余，那么就需要合并剩余的空间。完成合适的分配之后需要更新当前的状态量，例如内存块的标记与属性，以及当前系统中的空闲页数量。

```c
if (page != NULL) {
    list_entry_t* prev = list_prev(&(page->page_link));//page在空闲页链表中的前一个节点
    list_del(&(page->page_link));//将被分配内存块从空闲页链表中删除
    ////如果被分配内存块的大小大于分配请求的页数n，则需要将剩余部分重新加入空闲页链表
    if (page->property > n) {
        struct Page *p = page + n;//计算出剩余部分的内存页起始位置，保存在指针P中
        p->property = page->property - n;//剩余的物理页数量=原始内存块大小-已分配内存
        SetPageProperty(p);//更新P的属性，表示他是当前内存块的首页(设置标志位：PG_property = 0 PG_reserved = 1)
        list_add(prev, &(p->page_link));//将剩余页块添加到空闲页块链表中
    }
    nr_free -= n;//更新系统中的空闲页数量
    ClearPageProperty(page);//将被分配内存块page的属性标记清除
}
```

#### 4.内存释放

内存释放时，首先要更新所有待释放的页面的标志、属性以及引用次数。对于释放的内存块首页，需要更新当前的空闲页面的数量，并且更新整个系统中的空闲页数量。

```C
assert(n > 0);//确保释放的内存块中有需要释放的页面
struct Page *p = base;//待释放的内存块首地址
for (; p != base + n; p ++) {//遍历完要释放的区域
    assert(!PageReserved(p) && !PageProperty(p));//确保既不是保留页也不是已分配页
    p->flags = 0;//将内存页p的标志位清零，表示该内存页已经被释放
    set_page_ref(p, 0);//将内存页p的引用计数设置为零，表示没有引用该内存页
}
base->property = n;//起始内存页base的property属性设置为释放的页数n，表示这些页现在是连续的空闲页块
SetPageProperty(base);//将起始内存页base的属性标记为已分配，因为这个内存块已经被分配出去
nr_free += n;//增加系统中的空闲页数量
```

接下来就需要将这个空闲的内存块添加到空闲列表中。按照地址排序插入到合适的位置中，需要分为三种情况：插入到表头、表中、表尾。与内存分配相类似，不再赘述。

```C
if (list_empty(&free_list)) {//检查空闲列表是否为空
    list_add(&free_list, &(base->page_link));//如果为空直接将当前的 base 页面块添加到空闲列表
} else {//如果空闲列表不为空，遍历列表中的页面块
    list_entry_t* le = &free_list;//初始化为指向空闲列表的头部
    while ((le = list_next(le)) != &free_list) {
        struct Page* page = le2page(le, page_link);//当前 le 指向的页面块转换为 struct Page 结构
        if (base < page) {//找到第一个大于Base地址的页面块
            list_add_before(le, &(base->page_link));//在当前页面块之前插入 base
            break;
        } else if (list_next(le) == &free_list) {//已经到达链表结尾
            list_add(le, &(base->page_link));//插入表尾
   		}
    }
}
```

在内存释放时的一个关键操作是合并内存块。如果所释放的内存与前一个内存块相邻(`p + p->property == base`)或者后一个内存块相邻(`base + base->property == p`)，就需要将这个内存块与之合并。

检查前一个内存块是否需要合并的代码如下：

```C
list_entry_t* le = list_prev(&(base->page_link));//前一个内存块
    if (le != &free_list) {//如果空闲列表不为空
        p = le2page(le, page_link);//le 转换为 struct Page 结构
        if (p + p->property == base) {//检查前一个内存块p的结束地址是否等于base内存块的开始地址
            //如果是的话，这表示它们相邻并且可以合并
            p->property += base->property;
            ClearPageProperty(base);//清除base内存块的属性，表示它不再是一个独立的空闲内存块
            list_del(&(base->page_link));//从空闲列表中删除base内存块
            base = p;//更新 base 指针，使其指向合并后的内存块
        }
    }
```

检查后一个内存块的是否需要合并的代码与之类似，不再赘述。

```c
le = list_next(&(base->page_link));//后一个内存块
    if (le != &free_list) {//如果空闲列表不为空
        p = le2page(le, page_link);//le 转换为 struct Page 结构
        if (base + base->property == p) {//检查后一个内存块p的开始地址是否等于base内存块的结束地址
            base->property += p->property;//将base页面块增加p内存块的大小
            ClearPageProperty(p);//清除p内存块的属性，表示它不再是一个独立的空闲内存块
            list_del(&(p->page_link));//从空闲列表中删除p内存块
        }
    }
```

完成代码后，还需要将内存管理器设置为 *best-fit* 算法的管理器，修改`pmm.c/init_pmm_manager`中的`init_pmm_manager()`函数如下：

```C
static void init_pmm_manager(void) {
    pmm_manager = &best_fit_pmm_manager;
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}
```

执行`make grade`得分如下：

![image-20231014151305239.png](https://s2.loli.net/2023/10/14/LveqKX5Ri6NAZnQ.png)

### (二)你的 Best-Fit 算法是否有进一步的改进空间？

Best_Fit算法在仿照First_Fit算法的基础上实现，没有对时间复杂度进行改进，因此具有很大的改进空间。

#### (1)外部碎片问题

Best_Fit算法找到第一个能够满足需求的内存块进行内存分配，将剩余的内存分区放回空闲列表。在这个过程存在一些问题。例如在分配内存块后，被放回空闲列表中的剩余内存块可能很小，无法再重新利用，在这种情况下切割这部分碎片空间花费了一定的时间，但却换来了空间上的浪费，导致算法产生了不必要的开销。

在这种情况下Bset_Fit可以采用灵活的动态碎片管理策略。如果内存充足，可以考虑放弃切割内存块，节省分配时间。如果内存不充足，再进行内存块的切割。

#### (2)查找效率问题

在Best_Fit算法中，需要遍历整个空闲列表来找到最小的满足需求的内存块。与`first fit`算法相类似，无论是`alloc`过程还是`free`过程都需要`O(n)`的复杂度，因此可以采用First_Fit的优化方法，使用`splay`等平衡二叉树结构来取代简单的链表结构来维护空闲块，从而快速找到大小最合适的内存块。

除此之外，Best_Fit的改进方向也可以与Buddy_System相类似，先将内存划分为不同大小的段，每个段内只包含一种大小的内存块。当需要分配内存时，先确认该内存大小属于哪个段中，再在这些段内的内存块中进行查找。通过减少需要查找的总内存块的数量来降低查找时间。

#### (3)合并开销问题

在Best_Fit算法中，每一次释放内存后都需要合并内存块，在合并上有许多开销。在合并策略上，Bset_Fit也可以采用动态策略，如果当前系统中有较多的大空间内存块，就不立即合并相邻的空闲块；如果系统中缺少可用的大空间内存块，再进行合并操作。

## 扩展练习Challenge 1 ：buddy system(伙伴系统)分配算法(需要编程)

Buddy System算法把系统中的可用存储空间划分为存储块(Block)来进行管理, 每个存储块的大小必须是2的n次幂(Pow(2, n)), 即1, 2, 4, 8, 16, 32, 64, 128…

- 参考[伙伴分配器的一个极简实现](http://coolshell.cn/articles/10427.html)， 在ucore中实现buddy system分配算法，要求有比较充分的测试用例说明实现的正确性，需要有设计文档。

参见独立的 [Buddy System 设计文档](https://github.com/Herren-Hsing/Operation-System/blob/main/Lab2/Buddy_System_Design_Doc.md)。

## 扩展练习Challenge 2 ：任意大小的内存单元 slub 分配算法(需要编程)

slub算法，实现两层架构的高效内存单元分配，第一层是基于页大小的内存分配，第二层是在第一层基础上实现基于任意大小的内存分配。可简化实现，能够体现其主体思想即可。

- 参考[linux的slub分配算法/](http://www.ibm.com/developerworks/cn/linux/l-cn-slub/)，在ucore中实现slub分配算法。要求有比较充分的测试用例说明实现的正确性，需要有设计文档。

参见独立的 [Slub 设计文档](https://github.com/Herren-Hsing/Operation-System/blob/main/Lab2/Slub_Design_Doc.md)。

## 扩展练习 Challenge 3 ：硬件的可用物理内存范围的获取方法(思考题)

> 如果 OS 无法提前知道当前硬件的可用物理内存范围，请问你有何办法让 OS 获取可用物理内存范围？

### CPU主动探测

80386时代，可以通过写入AA、读回AA，写入55、读回55探测可用物理内存。因此在未知物理内存时，可以使用类似的方式进行内存探测。

### 借助BIOS探测内存

#### BIOS Function: `INT 0x15, EAX = 0xE820`

当使用这个命令时，系统将返回一个未排序的内存描述符列表，每个描述符描述系统中的一个物理内存区域。

每个内存描述符包含以下信息：

- 第一个 uint64_t = 基地址
- 第二个 uint64_t = “region”的长度(如果该值为 0，则忽略该条目)
- 接下来 uint32_t = 区域“类型”        
  - 类型 1：可用(正常)RAM
  - 类型 2：保留 - 不可用
  - 类型 3：ACPI 可回收内存
  - 类型 4：ACPI NVS 内存
  - 类型 5：包含坏内存的区域
- 接下来 uint32_t = ACPI 3.0 扩展属性位字段(如果返回 24 个字节，而不是 20 个字节)        
  - 扩展属性的位 0 指示是否应忽略整个条目(如果该位已清除)。这将是一个巨大的兼容性问题，因为大多数当前操作系统不会读取此位，也不会忽略该条目。
  - 扩展属性的位 1 指示该条目是否是非易失性的(如果该位已设置)。该标准指出“报告为非易失性的内存可能需要进行表征以确定其是否适合用作传统 RAM”。
  - 扩展属性的剩余 30 位当前未定义。

#### 其他BIOS命令

1. BIOS Function: INT 0x15, AX = 0xE881

   该功能在实模式下不起作用。相反，它应该从 32 位保护模式调用。它返回与函数 E801 相同的信息，但使用扩展寄存器 (EAX/EBX/ECX/EDX)。

2. BIOS Function: INT 0x15, AX = 0xE801

   为处理 15M 内存空洞而构建的，但会停在其上的下一个空洞/内存映射设备/保留区域。也就是说，它只设计用于处理16M以上的连续内存。

   典型输出：

   AX = CX = 1M 到 16M 之间的扩展内存，以 K 为单位(最大 3C00h = 15MB)

   BX = DX = 16M 以上的扩展内存，以 64K 块为单位

3. BIOS Function: INT 0x15, AX = 0xDA88

   返回在物理内存地址0x00100000开始处的连续可用RAM的大小，以KiB为单位。它的返回值包括在BX寄存器中。由于这个功能提供的信息只包括从0x00100000地址开始的内存，如果需要了解更多内存的信息，应在0x01000000地址开始探测是否存在更多可用的RAM。

4. BIOS Function: INT 0x15, AH = 0x88

   只能识别最大64MB的内存。即使内存容量大于64MB，也只会显示63MB。

5. BIOS Function: INT 0x15, AH = 0x8A

   用于返回扩展内存(extended memory)的大小，以KiB为单位。它返回连续可用RAM的大小，从物理内存地址0x00100000开始，结果以DX:AX寄存器对返回。由于这个功能提供的信息只包括从0x00100000地址开始的内存，如果需要了解更多内存的信息，应在0x01000000地址开始探测是否存在更多可用的RAM。

6. BIOS Function: INT 0x15, AH = 0xC7

   提供了很好的内存映射：

   ```
   Size   Offset  Description                                  
   
    2      00h     Number of significant bytes of returned data (excluding this uint16_t)
    4      02h     Amount of local memory between 1-16MB, in 1KB blocks
    4      06h     Amount of local memory between 16MB and 4GB, in 1KB blocks
    4      0Ah     Amount of system memory between 1-16MB, in 1KB blocks
    4      0Eh     Amount of system memory between 16MB and 4GB, in 1KB blocks
    4      12h     Amount of cacheable memory between 1-16MB, in 1KB blocks
    4      16h     Amount of cacheable memory between 16MB and 4GB, in 1KB blocks
    4      1Ah     Number of 1KB blocks before start of nonsystem memory between 1-16MB
    4      1Eh     Number of 1KB blocks before start of nonsystem memory between 16MB and 4GB
    2      22h     Starting segment of largest block of free memory in 0C000h and 0D000h segments
    2      24h     Amount of free memory in the block defined by offset 22h
   ```

​      第一个 uint16_t 可以返回的最小数字是 66 个字节。以下是内存类型的定义方式：

- 系统板上的本地内存或无法从通道访问的内存。它可以是系统内存或非系统内存。
- 适配器上的通道内存。它可以是系统内存或非系统内存。
- 由主操作系统管理和分配的系统内存。如果启用了缓存，则该内存将被缓存。
- 非系统内存，不由主操作系统管理或分配。该存储器包括存储器映射的I/O设备；位于适配器上并且可以由适配器直接修改的内存；以及可以在其地址空间内重新定位的存储器，例如存储体交换和扩展存储器规格(EMS)存储器。该内存没有被缓存。

### Device Tree

由硬件开发人员提供，并按照源代码的标准编写，由DT Compiler编译成二进制程序，由openSBI传给OS，OS解析得到。DTS采用树形结构描述板级设备，也就是开发板上的设备信息，比如CPU 数量、 内存基地址、IIC 接口上接了哪些设备、SPI 接口上接了哪些设备等。

物理内存的大小会在DTS(Device Tree Source，设备树)中描述，如下dts的描述：

```c
/dts-v1/;
/ {
	#address-cells = <2>;
	#size-cells = <2>;
	compatible = "kendryte,k210";

	chosen {
                bootargs = "console=hvc0 earlycon=sbi";
	};

	cpus {
		#address-cells = <1>;
		#size-cells = <0>;
		cpu0: cpu@0 {
			device_type = "cpu";
			clock-frequency = <390000000>;
			i-cache-size = <32768>;
			d-cache-size = <32768>;
			mmu-type = "none";
			reg = <0>;
			riscv,isa = "rv64imafdc";
			status = "okay";
			cpu0_intc: interrupt-controller {
				#interrupt-cells = <1>;
				compatible = "riscv,cpu-intc";
				interrupt-controller;
			};
		};
		cpu1: cpu@1 {
			device_type = "cpu";
			clock-frequency = <390000000>;
			d-cache-size = <32768>;
			i-cache-size = <32768>;
			mmu-type = "none";
			reg = <1>;
			riscv,isa = "rv64imafdc";
			status = "okay";
			cpu1_intc: interrupt-controller {
				#interrupt-cells = <1>;
				compatible = "riscv,cpu-intc";
				interrupt-controller;
			};
		};
	};

	memory@80000000 {
		/* Bank 0: 4 MB, Bank 1: 2 MB, AI chip SRAM: 2MB */
		device_type = "memory";
		reg = <0x00000000 0x80000000 0x00000000 0x00800000>;
	};

	plic0: interrupt-controller@C000000 {
		#interrupt-cells = <1>;
		compatible = "riscv,plic0";
		interrupt-controller;
		interrupts-extended =
			<&cpu0_intc 11 &cpu0_intc 9
			 &cpu1_intc 11 &cpu1_intc 9>;
		reg = <0x0 0xc000000 0x0 0x4000000>;
	};
};

```

其中描述内存分布的代码如下：

```c
	memory@80000000 {
		/* Bank 0: 4 MB, Bank 1: 2 MB, AI chip SRAM: 2MB */
		device_type = "memory";
		reg = <0x00000000 0x80000000 0x00000000 0x00800000>;
	};
```

- `memory@80000000`: 这是内存节点的名称，通常由一个 "@" 符号和一个地址组成。在这里，内存节点的名称是 "memory"，而其起始地址是 0x80000000。

- `device_type = "memory"`: 这一行指定了这个节点的设备类型，说明这是一个内存节点。

- `reg = <0x0 0x80000000 0x0 0x80000000>`: 这一行定义了内存节点的 `reg` 属性，它提供了内存的起始地址和大小信息。在这里，`reg` 属性的值是一个四元组，每个元素都是一个32位的值，描述了从物理地址0x0到0x80000000的内存范围，总共2 GB的内存。

在openSBI中，fdt_reserved_memory_fixup(fdt)函数可以**检查设备树中的保留内存信息**：它会解析设备树，查找并分析设备树中定义的保留内存区域。

由此，可以OS可以解析该设备树得到内存布局。

### sbi_query_memory

openSBI中的sbi_query_memory可以提供类似int 15H的功能，可以向S型SBI扩展(S-mode SBI Extension)请求物理内存布局信息。它可以帮助操作系统或其他软件组件了解系统中可用的物理内存范围、每个内存区域的大小、类型和属性等信息。

### SMBIOS

SMBIOS 旨在允许“管理员”评估硬件升级选项或维护公司当前正在使用的硬件的目录(即，它提供供人类使用的信息，而不是供软件使用的信息。SMBIOS 可以告知已安装的内存条数量及其大小(以 MB 为单位)。SMBIOS 可以从保护模式调用。

### CMOS

在一些情况下，CMOS可能包含一些基本的内存参数，如安装的内存数量和总内存容量。然而，这些信息通常是有限的，可能不包括有关内存孔、内存映射设备或其他保留区域的详细信息。另外，CMOS内存大小信息可能忽略标准内存孔(memory holes)，这可以导致内存大小的不准确。

### 参考资料

https://wiki.osdev.org/Detecting_Memory_(x86)

https://msyksphinz-hatenablog-com.translate.goog/entry/2018/10/04/040000?_x_tr_sl=ja&_x_tr_tl=zh-CN&_x_tr_hl=zh-CN&_x_tr_pto=sc

https://zhuanlan.zhihu.com/p/605824260?utm_id=0

## 知识点总结

### (一) 物理地址和虚拟地址

#### (1)本实验中的知识点

物理地址是指由硬件产生的内存中的真实存储地址；虚拟地址也称为逻辑地址，是进程加载后CPU所生成的地址。

在本次实验中，为了将虚拟地址转化为物理地址，我们在entry.S文件中构建一个三级页表，使得虚拟地址经过页表的翻译恰好变成物理地址。在RISCV架构中，我们使用`satp`控制寄存器中指向这个页表，通过页表的根节点的物理页号，使用地址的时候都要经过这个页表的翻译，把**虚拟地址所在的虚拟页映射到一个物理页帧**，然后再在这个物理页帧上根据**页内偏移**找到物理地址，从而完成映射。

#### (2)对应的OS原理中的知识点

在X86体系中，当CPU执行一条指令时需要加载某一虚拟地址内存时，会将这个虚拟地址传给MMU，MMU查询页表并返回物理地址，CPU中的控制器将物理地址和相关的总线控制信号传输到总线上后，存储信号根据总线上的地址和控制信号判断要进行读操作还是写操作，如果是写操作就将数据写入到指定的存储单元中，如果是读操作，就将指定的内存单元中的数据读出，放回数据总线，由CPU读取。

#### (3)知识点异同

**相同点:**    在X86与RISCV体系中，物理地址与虚拟地址的映射都是通过页表来实现的，页表均由操作系统管理控制。

**不同点：**

1. X86通常使用两级页表结构，而本实验中的RISCV使用的是三级页表结构
2. X86的页表由MMU控制，而RISCV使用控制寄存器Satp指向页表基址实现地址转化。

### (二) 分配算法

#### (1)本实验中的知识点

在本实验中，主要通过学习First_Fit算法来模仿实现了Bset_fit算法。本实验中使用双向链表来实现内存块的管理，最重要的两个函数是内存块的分配与释放。在练习12中已做详细阐释，此处不再赘述。

本实验中的挑战1还实现了伙伴系统中的内存分配。有一个需要分配的内存时，由小到大在空闲块数组中找最小的可用空闲块。如果空闲块过大，对可用空闲块进行二等分，直到得到合适的可用空闲块。释放时将大小相同、地址相邻且低地址空闲块起始地址为2i＋1的位数的两个空闲块进行合并，从而实现了一个空闲块按大小和起始地址组织成二维数组为数据结构的分配方法。

如果当前结点够用，就/2找该结点的做孩子，查看是否够用；反之不够用就找父节点。产生碎片时，如果是128k分配了100k的空间，就不再进行合并处理(因为要保持2倍数的关系)

#### (2)对应的OS原理中的知识点

除了最佳匹配、最先排序外，还有两种基础分配算法。

一是最差匹配，与最佳匹配的算法方法相类似，最差匹配可以通过维护一个max变量，通过扫描链表来找到一个最大的内存块。最差匹配基本不留下小空闲分区，但较大的空闲分区不会被保留。

最优最差匹配算法中搜索都需要O(n)级别的算法复杂度，我们可以使用堆排序进行改进。将所有的内存块按照大小排序，取走最大或者最小的一个内存块，不仅保持取块时时间复杂度小，在用完块释放时也要能够地找到合适的位置，根据这种数据结构的存储思想可以引申出伙伴系统。

二是在first-fit上的变种，下次匹配法。按分区的先后次序，从上次分配的分区起查找，到最后分区时再回到开头，找到符合要求的第一个分区就分配。这种方法无须每次都从开头寻找，分配和释放的时间性能较好，使空闲分区分布得更均匀，但较大的空闲分区不易保留。
