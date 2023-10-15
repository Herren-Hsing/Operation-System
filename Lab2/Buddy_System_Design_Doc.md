# Buddy System 设计文档

>Contributors：焦心雨(2112536)、李艺楠(2110246)、辛浩然(2112514)
>
>[GitHub链接](https://github.com/Herren-Hsing/Operation-System)

## 基本思想

`Buddy System`是一种用于管理内存分配和释放的算法，它的基本思想是将内存分割成一系列大小相等的块，每个块的大小都是2的幂次方（例如，2^0^、2^1^、2^2^、2^3^等），这些块组成了一个二叉树结构，其中根节点表示整个可用内存，而叶子节点表示最小的内存块。除根结点外，每个节点都有一个与之关联的伙伴节点。

1. 初始状态：将整个可用内存看作一个根节点，大小为2的幂次方（通常是2的某个整数次方，例如2^10^，表示1024个字节）。根节点表示整个可用内存。
2. 分配内存：当需要分配一定大小的内存时，`Buddy`系统会从伙伴系统树的根节点开始搜索，找到一个大小大于等于所需内存的最小块（通常是最接近但不小于所需大小的2的幂次方块）。然后，将这个块一分为二，形成两个伙伴节点，其中一个分配给请求的内存，而另一个保留在可用内存中。
3. 释放内存：当要释放分配的内存时，`Buddy`系统会将被释放的内存块标记为空闲，并尝试与其伙伴节点合并（如果伙伴节点也是空闲的）。合并后，继续尝试与更高级别的伙伴节点合并，直到不能再合并为止。这可以有效地合并连续的空闲内存块，以便再次分配。

## 算法实现

> 我们参考[伙伴分配器的一个极简实现](http://coolshell.cn/articles/10427.html)， 在`ucore`中实现了`buddy system`分配算法。

算法实现的整体思想是，通过一个数组形式的完全二叉树来监控管理内存，二叉树的节点保存可管理的内存大小和空闲标记，高层节点对应大的块，低层节点对应小的块。

首先我们添加`buddy_pmm.h`头文件，并定义页面管理器`buddy_pmm_manager`：

```c
#ifndef __KERN_MM_BUDDY_PMM_H__
#define  __KERN_MM_BUDDY_PMM_H__

#include <pmm.h>

extern const struct pmm_manager buddy_pmm_manager;

#endif /* ! __KERN_MM_BUDDY_PMM_H__ */
```

然后，在`buddy_pmm.c`中完善页面管理器的定义：

```c
const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};
```

接下来，我们的工作就是上述函数的实现。

### 初始化

首先定义分配器`buddy`的数据结构：

```c
struct buddy
{
    uint8_t level;         
    uint8_t longest[90000]; 
};
struct buddy root;
struct buddy *self = &root;
```

为了减少内存消耗，我们使用`uint8_t`类型，仅仅保存可管理的内存以2为底的对数。

- 成员`level`用于表示分配器管理的最大内存以2为底的对数（也是树的高度）。这通常用来确定分配器可以管理的最大内存块的大小，即2^level^；
- `longest`数组用来保存一个二叉树中每个节点管理内存的尺寸以2为底的对数（每个节点的高度）。每个元素表示一个节点的内存块的大小，大小以2的幂次方形式表示。
  - 我们认为，无论如何可管理的内存页数都不会到2^255^ 大小，因此将`0xFF`定为已分配节点的标记，即`longest`为`0xFF`代表该节点管理的内存都被分配。

我们认为，在我们实现的`buddy system`中没有必要维护双向链表以记录空闲页面，因此不涉及`free_area_t`结构体。而为记录空闲页总数，我们另外定义了全局变量`num_free`，并在`buddy_init`函数中将其初始化为0。

接下来，我们实现`buddy_init_memmap`，在获取到可管理的物理内存空间后，对分配器进行更详细的初始化。

- `buddy_init_memmap`函数接受两个参数，`base`和`n`。`base`表示可管理的物理内存空间的起始位置。`n`表示可管理的物理内存空间中的总页数。
- 首先，确保`n`大于0；

- 将起始页指针`base`赋给全局变量`page_base`，以表示可管理的物理内存空间的起始页；
- 计算`n`的以2为底的对数并向下取整，这就是`buddy system`实际管理的页数以2为底的对数；计算实际可用的内存页数`actual_n`，这个值是2的`allocpages_level`次方，表示实际管理物理页数；
- 增加全局变量`num_free`的值，增加`actual_n`页。

- 接下来，从`base`通过循环遍历`actual_n`个页的范围，对每个页进行修改：

  - 将页的`flags`字段重置为0；
  - 我们认为，原始`page`结构体中的`property`和`PG_property`在`buddy system`页面管理中没有用处，因此，对于这些页，我们初始化`property = 1`，但在之后不会使用。
    - 而对于`PageProperty`标记，在`first_fit`中代表的含义是该页是连续空闲页的首页。在这里，我们改变其意义，标记`PageProperty`则说明该页是一个空闲页，不标记则说明该页已被分配。因此，需要设置这些页的`PageProperty`标记，标记这些页是空闲的；

  - 清除引用此页的虚拟页个数，表示此物理页没有被虚拟页引用。

- 最后，调用`buddy_new`函数，初始化创建的`buddy`数据结构，用于管理可用的物理内存页。

> 代码中所涉及的`log2_floor`、`pow2`等数学函数我们在自定义的辅助文件`math_self.h`中实现，该文件在`lab2/libs`目录下。为优化代码表现，我们使用移位实现求幂和求对数等数学操作。

```c
static void
buddy_init_memmap(struct Page *base, size_t n)
{
    assert(n > 0);
    struct Page *p = base;
    page_base = base; // 可管理的物理内存空间起始页

    uint8_t allocpages_level = log2_floor(n);   // 总内存页大小求log2并向下取整
    unsigned actual_n = pow2(allocpages_level); // 空闲页数
    num_free += actual_n;
    for (; p != base + actual_n; p++)
    {
        assert(PageReserved(p));
        p->flags = 0;
        p->property = 1;
        SetPageProperty(p);
        set_page_ref(p, 0);
    }
    
	// 传入所需要表示的总内存页大小，让buddy system的数组得以初始化
    buddy_new(self, allocpages_level);
}
```

接下来，我们实现`buddy_new`函数，通过初始化我们创建的`buddy`数据结构来维护一颗完全二叉树。

- 初始化结构体的`level`属性，赋值为传入的参数，表示管理的最大内存以2为底的对数，也代表了二叉树的高度；

- 初始化`longest`数组，计算二叉树所有节点的高度，也就是节点能管理的内存大小以2为底的对数。

```c
static void buddy_new(struct buddy *s, uint8_t level)
{
    if (level < 0)
        return;

    s->level = level;

    int total_size = 2 * pow2(level) - 1;
    uint8_t node_level = level + 1;
    for (int i = 0; i < total_size; ++i)
    {
        if (is_power_of_2(i + 1))
            node_level--;
        s->longest[i] = node_level;
    }
}
```

### 分配页面

以一实例说明分配的基本思路：

<img src="https://s2.loli.net/2023/10/07/gsESLZUYjM6Houw.png" alt="image.png" style="zoom:50%;" />

在分配阶段，首先要搜索大小适配的块，假设第一次分配3，转换成2的幂是4，我们先要对整个内存进行对半切割，从16切割到4需要两步，那么从下标[0]节点开始深度搜索到下标[3]的节点并将其标记为已分配。第二次再分配3那么就标记下标[4]的节点。第三次分配6，即大小为8，那么搜索下标[2]的节点，因为下标[1]所对应的块被下标[3~4]占用了。

接下来具体说明我们的实现思路：

- `buddy_alloc_pages`函数接受一个参数 `n`，表示需要分配的物理内存页的数量。

- 首先，确保 `n` 大于0，即需要分配的页数必须是正数。并检查需要分配的页数 `n` 是否大于可用的空闲页数 `num_free`。如果 `n` 大于 `num_free`，表示没有足够的空闲页来满足分配请求，返回 `NULL`，表示分配失败。
- 接下来，计算需要分配的页数 `n` 的对数并向上取整，将结果存储在 `alloc_level` 中。计算需要分配的内存块的实际页数 `alloc_num`，这是2的 `alloc_level` 次方。
- 调用 `buddy_alloc` 函数，传递 `alloc_level` 作为参数，以执行实际的分配操作，并获得分配页相对于根节点的偏移量 `offset`。
  - 如果分配失败，`buddy_alloc` 函数返回`-1`，返回 `NULL` 表示分配失败。
  - 如果分配成功，代码根据偏移量 `offset` 找到分配的物理页面，将其赋给 `struct Page` 类型的指针 `page`。

- 减少全局变量 `num_free` 的值，减少量为分配的页数`alloc_num`。
- 通过循环遍历分配的页面，对每个页面调用 `ClearPageProperty` 函数来清除空闲页标记，将这些页面标记为已占用的。
- 返回分配的第一个页面的指针 `page`，即分配成功的物理内存页的起始地址。

```c
static struct Page *
buddy_alloc_pages(size_t n)
{
    assert(n > 0);    // 判断n是否大于0
    if (n > num_free) // 需要分配的页的个数大于空闲页的总数，直接返回
    {
        return NULL;
    }

    uint8_t alloc_level = log2_ceil(n); // 需要的页数求log2并向上取整
    unsigned alloc_num = pow2(alloc_level);
    unsigned offset = buddy_alloc(alloc_level); // 进行分配，得到分配首页相对根节点的偏移量

    if (offset == -1) // 分配异常
        return NULL;

    // 根据偏移，找到分配的页面
    struct Page *page = page_base + offset;
    num_free -= alloc_num;

    // 标记为占用页
    for (struct Page *p = page; p != page + alloc_num; p++)
    {
        ClearPageProperty(p);
    }

    return page;
}
```

接下来，实现`buddy_alloc`页面分配函数，这是`buddy system`的关键所在：

- 如果传入的 `level` 值是否小于0，将 `level` 设置为0，以确保 `level` 不会小于0。
- 检查根节点：如果 `self->longest[0]` 小于 `level` 或者等于`0xFF`（这是我们对于已分配节点的标记），则无法分配满足要求的内存块，返回`-1`表示分配失败。
- 符合基本条件后，开始<u>搜索一个合适的节点来分配内存块</u>。
  - 检查当前节点的左子树和右子树的 `longest` 值是否都大于等于 `level` 并且不等于`0xFF`，即能**满足空间**且**未分配**。
    - 如果两个子树都符合条件，选择其中最小的 `longest` 值所在的子树继续搜索。
    - 如果只有一个子树符合条件，就选择这个子树继续搜索。
  - 上述搜索直到搜索到的节点管理的内存刚好等于要分配的内存时为止。
  - 一旦找到一个合适的节点，将该节点的 `longest` 值设置为`0xFF`，表示已经被占用。

- 然后，通过调用 `modify_subtree` 函数来标记该节点子树的所有节点都被占用。
- 接着，计算分配的首页相对于根节点页面的偏移量 `offset`，并通过<u>逐级向上回溯的方式，更新父节点</u>的 `longest` 值，确保它是其两个子节点的 `longest` 值中的最大值。
- 最终，返回分配的首页相对于根节点页面的偏移量 `offset`，表示分配成功。

```c
static unsigned buddy_alloc(uint8_t level)
{
    if (self == NULL)
        return -1;
    if (level < 0)
        level = 0;
    if (self->longest[0] < level || self->longest[0] == 0xFF)
        return -1;
    unsigned index = 0;
    uint8_t node_level = self->level;
    // 从根节点开始，搜索左右子树
    for (; node_level != level; node_level--)
    {
        if (self->longest[LEFT_LEAF(index)] >= level && self->longest[LEFT_LEAF(index)] != 0xFF)
        {
            if (self->longest[RIGHT_LEAF(index)] >= level && self->longest[RIGHT_LEAF(index)] != 0xFF)
            {
                // 都符合条件，沿着最小值所在子树搜索
                if (self->longest[LEFT_LEAF(index)] > self->longest[RIGHT_LEAF(index)])
                {
                    index = RIGHT_LEAF(index);
                }
                else
                {
                    index = LEFT_LEAF(index);
                }
            }
            else
            {
                index = LEFT_LEAF(index);
            }
        }
        else
        {
            index = RIGHT_LEAF(index);
        }
    }
    self->longest[index] = 0xFF; // 标记为已占用
    modify_subtree(index);       // 将子树都标记为已占用
    unsigned offset = (index + 1) * pow2(node_level) - pow2(self->level);
    // 层层向上回溯，改变父节点的值，父节点longest值是两个子结点的最大值
    while (index)
    {
        index = PARENT(index);
        self->longest[index] = max_special(self->longest[LEFT_LEAF(index)], self->longest[RIGHT_LEAF(index)]);
    }
    return offset;
}
```

### 释放页面

继续[分配页面](###分配页面)一开始的例子，在释放阶段，我们依次释放上述第一次和第二次分配的块，即先释放[3]再释放[4]，当释放下标[4]节点后，我们发现之前释放的[3]是相邻的，于是我们立马将这两个节点进行合并，这样一来下次分配大小8的时候，我们就可以搜索到下标[1]适配了。若进一步释放下标[2]，同[1]合并后整个内存就回归到初始状态。

实现时，查找合适的节点进行释放，并在释放阶段，我们将之前分配出去的内存占有情况还原，并考察能否和同一父节点下的另一节点合并，而后递归合并，直至不能合并为止。并且需要更新相应的状态以反映内存块的释放情况。

具体实现步骤如下：

- `buddy_free_pages`函数接受两个参数，`base`和`n`，其中`base`是需要释放的物理内存的起始位置，`n`表示需要释放的物理内存页的数量。

- 首先，计算需要释放的页数 `n` 的对数并向上取整，将其存储在`free_level`中，并计算需要释放的实际页数`free_num`，这是2的`free_level`次方。

- 计算释放的物理内存页的起始页相对于根节点页的偏移量`offset`，并确保偏移量的有效性，即确保`self`存在且偏移量在有效范围内。

- 将`index`初始化为`offset + pow2(self->level) - 1`，表示要释放的起始页在树中的节点索引。接下来<u>搜索要释放的合适节点</u>：

  - 从当前节点开始遍历其祖先节点，直到找到一个已分配的节点，且该节点管理的内存与释放的内存大小相等。
  - 如果找到了这样的节点，循环中断，否则继续循环，直到根节点，如果未找到合适的节点则返回。

  - 找到了合适的需要释放的节点后，调用`assign_depth`函数将该节点`longest`值修改为高度（也就是管理内存大小的以2为底的对数），同时需要修改该节点的所有子节点的`longest`值为相应高度，以便反映已释放的状态。

- 接下来，向上遍历二叉树，尝试<u>合并相邻的空闲块</u>。

  - 计算左子树和右子树的`longest`值，并计算当前节点管理内存的大小。
    - 如果左子树和右子树的`longest`值之和等于节点管理内存的大小，说明可以合并这两个子树，将当前节点`longest`值标记为合并后的大小以2为底的对数。
    - 如果左子树和右子树不能合并，将当前节点的`longest`值设置为左右子树`longest`值的最大值。
    - 继续向上遍历，直到根节点。

- 增加`num_free`的值，增加量为释放的实际页数`free_num`。

- 将释放的页标记为空闲页。

```c
static void
buddy_free_pages(struct Page *base, size_t n)
{
    // 释放的页面数目
    uint8_t free_level = log2_ceil(n);
    int free_num = pow2(free_level);

    unsigned offset = base - page_base; // 释放首页相对根节点页的偏移
    assert(self && offset >= 0 && offset < pow2(self->level));

    uint8_t node_level = 0;
    // 把单页相对根节点页的偏移转换成该单页在树中的节点序号
    unsigned index = offset + pow2(self->level) - 1;

    for (;; index = PARENT(index))
    {
        // 从该节点向上找其父母节点，找到一个能满足大小且已经被分配的节点
        if (self->longest[index] == 0xFF && node_level == free_level)
            break;
        node_level++;
        if (index <= 0)
            return;
    }

    // 将该节点释放，需要修改其所有子节点的longest值
    assign_depth(free_level, index);

    // 向上合并，修改父母节点的记录值
    unsigned left_longest, right_longest, node_size;
    while (index)
    {
        index = PARENT(index);

        node_level++;
        if (self->longest[LEFT_LEAF(index)] == 0xFF)
            left_longest = 0;
        else
            left_longest = pow2(self->longest[LEFT_LEAF(index)]);
        if (self->longest[RIGHT_LEAF(index)] == 0xFF)
            right_longest = 0;
        else
            right_longest = pow2(self->longest[RIGHT_LEAF(index)]);

        node_size = pow2(node_level);

        // 左右子树longest的值相加是否等于原空闲块满状态的大小，能够合并
        if (left_longest + right_longest == node_size)
        {
            self->longest[index] = log2_ceil(node_size);
        }
        else
        {
            // 不能合并，取最大值
            self->longest[index] = max_special(self->longest[LEFT_LEAF(index)], self->longest[RIGHT_LEAF(index)]);
        }
    }
    num_free += free_num;

    unsigned base_offset = index;
    while (LEFT_LEAF(base_offset) < 2 * pow2(self->level) - 1)
    {
        base_offset = LEFT_LEAF(base_offset);
    }
    base_offset = base_offset - pow2(self->level) + 1;
    // 标记为空闲页
    for (struct Page *p = page_base; p != page_base + free_num; p++)
    {
        SetPageProperty(p);
    }
}
```

### 测试函数

> 测试前，需要将`pmm_manager`修改为Buddy System，即`pmm_manager = &buddy_pmm_manager;`

在测试函数`buddy_check`中，我们首先调用了基本测试函数`basic_check`，该函数与`first_fit`管理器中的版本基本一致，进行基本的内存管理检查，以确保`Buddy System`的基本功能正常工作。

随后，我们自定义了更多的分配和释放测试，先后分配和释放了一系列不同大小的内存块，并断言它们的地址关系和可用空闲页数的变化。

```c
static void
buddy_check(void)
{
    basic_check();

    int ini_free = num_free;

    struct Page *A, *B, *C, *D, *E;
    assert((A = alloc_page()) != NULL);
    assert((B = alloc_page()) != NULL);
    assert(ini_free == num_free + 2);
    assert(A + 1 == B);
    buddy_free_pages(A, 2);
    assert(num_free == ini_free);

    assert((A = buddy_alloc_pages(1)) != NULL);
    assert((B = buddy_alloc_pages(27)) != NULL);
    assert((C = buddy_alloc_pages(33)) != NULL);
    assert((D = buddy_alloc_pages(121)) != NULL);
    assert((E = buddy_alloc_pages(8)) != NULL);
    assert(A + 8 == E);
    assert(A + 32 == B);
    assert(B + 32 == C);
    assert(C + 64 == D);
    buddy_free_pages(A + 8, 7);
    buddy_free_pages(A, 1);

    assert((A = buddy_alloc_pages(32)) != NULL);
    assert(A + 32 == B);
    buddy_free_pages(A, 32);
    buddy_free_pages(B, 30);
    buddy_free_pages(C, 33);
    assert((A = buddy_alloc_pages(128)) != NULL);
    assert(A + 128 == D);
    assert(256 + num_free == ini_free);
    buddy_free_pages(A, 254);

    visual_check();

    assert(num_free == ini_free);
}
```

为了更明了地观察内存变化，我们实现了可视化检查函数`visual_check`，可视化验证`Buddy System`页面分配和释放状态。

为了可视化显示`Buddy System`的状态，我们实现了`buddy_dump`函数。它会创建一个字符数组`canvas`来表示`Buddy System`的状态，然后根据分配情况将`canvas`中的字符设置为`'-'`或`'*'`，最终通过`cputs`函数打印出可视化的`Buddy System`状态。

受到输出的限制，我们需要将总页面数控制在一定数目。因此，我们先保存之前的页面分配状态，然后重新初始化`buddy`结构体，将可管理的页面数设定为2^5^ 。在测试之后再将初始状态恢复。

`visual_check`中的测试代码和样例如下，覆盖分配和释放的多种情况：

```c
static void
visual_check(void)
{
    cprintf("\nHere is the visual check in which the max size is 32:\n");
    struct Page *A, *B, *C, *D, *E;
    struct buddy root_save = root;
    struct buddy new_root;
    struct buddy *new_self = &new_root;
    buddy_new(new_self, 5);
    root = new_root;
    struct Page *page_save = page_base;
    assert((A = alloc_page()) != NULL);
    cprintf("A = alloc_page()\n");
    buddy_dump();
    assert((B = buddy_alloc_pages(6)) != NULL);
    assert(A + 8 == B);
    cprintf("B = buddy_alloc_pages(6)\n");
    buddy_dump();
    assert((C = buddy_alloc_pages(14)) != NULL);
    assert(B + 8 == C);
    cprintf("C = buddy_alloc_pages(14)\n");
    buddy_dump();
    assert((D = buddy_alloc_pages(5)) == NULL);
    cprintf("D = buddy_alloc_pages(5), failed\n");
    buddy_dump();
    buddy_free_pages(A, 1);
    cprintf("buddy_free_pages(A, 1)\n");
    buddy_dump();
    assert((D = buddy_alloc_pages(6)) != NULL);
    cprintf("D = buddy_alloc_pages(6)\n");
    buddy_dump();
    buddy_free_pages(D, 14);
    cprintf("buddy_free_pages(D, 14)\n");
    buddy_dump();
    assert((A = buddy_alloc_pages(8)) != NULL);
    cprintf("A = buddy_alloc_pages(8)\n");
    buddy_dump();
    buddy_free_pages(C, 8);
    cprintf("buddy_free_pages(C, 8)\n");
    buddy_dump();
    assert((C = buddy_alloc_pages(8)) != NULL);
    cprintf("C = buddy_alloc_pages(8)\n");
    buddy_dump();
    buddy_free_pages(A, 16);
    cprintf("buddy_free_pages(A, 16)\n");
    buddy_dump();
    buddy_free_pages(C + 20, 2);
    cprintf("buddy_free_pages(C + 20, 2)\n");
    buddy_dump();
    buddy_free_pages(C + 18, 3);
    cprintf("buddy_free_pages(C + 18, 3)\n");
    buddy_dump();
    assert((B = buddy_alloc_pages(30)) == NULL);
    cprintf("B = buddy_alloc_pages(30),failed\n");
    buddy_dump();
    assert((B = buddy_alloc_pages(15)) != NULL);
    cprintf("B = buddy_alloc_pages(15)\n");
    buddy_dump();
    assert((A = buddy_alloc_pages(3)) != NULL);
    cprintf("A = buddy_alloc_pages(3)\n");
    buddy_dump();
    assert((A = buddy_alloc_pages(6)) != NULL);
    cprintf("A = buddy_alloc_pages(6)\n");
    buddy_dump();
    assert((A = buddy_alloc_pages(2)) != NULL);
    cprintf("A = buddy_alloc_pages(2)\n");
    buddy_dump();
    buddy_free_pages(B, 32);
    cprintf("buddy_free_pages(B, 32)\n");
    buddy_dump();
    assert((A = buddy_alloc_pages(7)) != NULL);
    cprintf("A = buddy_alloc_pages(7)\n");
    buddy_dump();
    assert((B = buddy_alloc_pages(3)) != NULL);
    assert(A + 8 == B);
    cprintf("B = buddy_alloc_pages(3)\n");
    buddy_dump();
    assert((C = buddy_alloc_pages(4)) != NULL);
    assert(B + 4 == C);
    cprintf("C = buddy_alloc_pages(4)\n");
    buddy_dump();
    assert((D = buddy_alloc_pages(32)) == NULL);
    cprintf("D = buddy_alloc_pages(32), failed\n");
    buddy_dump();
    buddy_free_pages(A + 8, 8);
    cprintf("buddy_free_pages(A + 8, 8)\n");
    buddy_dump();
    buddy_free_pages(A, 8);
    cprintf("buddy_free_pages(A, 8)\n");
    buddy_dump();
    assert((D = buddy_alloc_pages(32)) != NULL);
    cprintf("D = buddy_alloc_pages(32)\n");
    buddy_dump();
    buddy_free_pages(A + 15, 16);
    cprintf("buddy_free_pages(A + 15, 16)\n");
    buddy_dump();
    buddy_free_pages(B + 9, 16);
    cprintf("buddy_free_pages(B + 9, 16)\n");
    buddy_dump();

    root = root_save;
    page_base = page_save;
    cprintf("visual check finished \n \n");
}
```

以下就是我们最终的终端运行输出结果，可以发现测试中的断言都正确，且可视化的分配状态也是正确的：

```
memory management: buddy_pmm_manager
physcial memory map:
  memory: 0x0000000007e00000, [0x0000000080200000, 0x0000000087ffffff].

Here is the visual check in which the max size is 32:
A = alloc_page()
*_______________________________
B = buddy_alloc_pages(6)
*_______********________________
C = buddy_alloc_pages(14)
*_______************************
D = buddy_alloc_pages(5), failed
*_______************************
buddy_free_pages(A, 1)
________************************
D = buddy_alloc_pages(6)
********************************
buddy_free_pages(D, 14)
________________****************
A = buddy_alloc_pages(8)
********________****************
buddy_free_pages(C, 8)
********________________********
C = buddy_alloc_pages(8)
****************________********
buddy_free_pages(A, 16)
________________________********
buddy_free_pages(C + 20, 2)
________________________****__**
buddy_free_pages(C + 18, 3)
______________________________**
B = buddy_alloc_pages(30),failed
______________________________**
B = buddy_alloc_pages(15)
****************______________**
A = buddy_alloc_pages(3)
****************________****__**
A = buddy_alloc_pages(6)
****************************__**
A = buddy_alloc_pages(2)
********************************
buddy_free_pages(B, 32)
________________________________
A = buddy_alloc_pages(7)
********________________________
B = buddy_alloc_pages(3)
************____________________
C = buddy_alloc_pages(4)
****************________________
D = buddy_alloc_pages(32), failed
****************________________
buddy_free_pages(A + 8, 8)
********________________________
buddy_free_pages(A, 8)
________________________________
D = buddy_alloc_pages(32)
********************************
buddy_free_pages(A + 15, 16)
________________****************
buddy_free_pages(B + 9, 16)
________________________________
visual check finished 
 
check_alloc_page() succeeded!
satp virtual address: 0xffffffffc0206000
satp physical address: 0x0000000080206000
```
