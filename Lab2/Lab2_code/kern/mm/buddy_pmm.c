#include <pmm.h>
#include <list.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <buddy_pmm.h>
#include <math_self.h> // 自定义辅助文件math_self.h，包含log2、pow2等数学函数的实现

unsigned num_free;

#define LEFT_LEAF(index) ((index) * 2 + 1)
#define RIGHT_LEAF(index) ((index) * 2 + 2)
#define PARENT(index) (((index) + 1) / 2 - 1)

// buddy分配器的数据结构
struct buddy
{
    uint8_t level;          // 最大尺寸的幂
    uint8_t longest[90000]; // 保存二叉树每个节点管理内存的尺寸的幂
};
struct buddy root;
struct buddy *self = &root;

struct Page *page_base;

static void
buddy_init(void)
{
    num_free = 0;
}

static size_t
buddy_nr_free_pages(void)
{
    return num_free;
}

// 初始化buddy分配器
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

// 初始化可管理的物理内存空间
static void
buddy_init_memmap(struct Page *base, size_t n)
{
    assert(n > 0);
    struct Page *p = base;
    page_base = base; // 可管理的物理内存空间起始页

    uint8_t allocpages_level = log2_floor(n);   // 总内存页大小求log2并向下取整
    unsigned actual_n = pow2(allocpages_level); // 空闲物理页数
    num_free += actual_n;
    // 修改页面状态
    // 这里没有修改原始Page结构体，但似乎Page结构体的property和PG_property在buddy system中没有用处
    // 这里将PG_property标记位的含义局限于是空闲页
    for (; p != base + actual_n; p++)
    {
        assert(PageReserved(p));
        p->flags = 0;
        p->property = 1;
        SetPageProperty(p);
        set_page_ref(p, 0);
    }

    buddy_new(self, allocpages_level); // 传入所需要表示的总内存页大小，让buddy system的数组得以初始化
}

static void modify_subtree(unsigned index)
{
    if (index > 2 * pow2(self->level) - 1)
        return;
    self->longest[index] = 0xFF;
    modify_subtree(LEFT_LEAF(index));
    modify_subtree(RIGHT_LEAF(index));
}

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

static void assign_depth(unsigned depth, unsigned index)
{
    if (index > 2 * pow2(self->level) - 1)
        return;
    self->longest[index] = depth;
    assign_depth(depth - 1, LEFT_LEAF(index));
    assign_depth(depth - 1, RIGHT_LEAF(index));
}

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

// 可视化页状态
void buddy_dump()
{
    char canvas[65];
    int i, j;
    unsigned node_size, offset;

    if (self == NULL)
    {
        cprintf("buddy_dump: self is NULL\n");
        return;
    }

    if (self->level > 6)
    {
        cprintf("buddy_dump: self is too big to dump\n");
        return;
    }

    // 空闲页用'-'表示
    memset(canvas, '_', sizeof(canvas));

    unsigned size = pow2(self->level);
    node_size = size * 2;
    for (i = 0; i < 2 * size - 1; ++i)
    {
        if (is_power_of_2(i + 1))
            node_size /= 2;

        // 最底层节点，被分配，相应位置标*
        if (self->longest[i] == 0xFF && i >= size - 1)
        {
            canvas[i - size + 1] = '*';
        }
        // 不是最底层，但左/右子节点分配，将相应范围置为*
        else
        {
            if (self->longest[LEFT_LEAF(i)] == 0xFF)
            {
                offset = (i + 1) * node_size - size;

                for (j = offset; j < offset + node_size / 2; ++j)
                    canvas[j] = '*';
            }
            if (self->longest[RIGHT_LEAF(i)] == 0xFF)
            {
                offset = (i + 1) * node_size - size;

                for (j = offset + node_size / 2; j < offset + node_size; ++j)
                    canvas[j] = '*';
            }
        }
    }
    canvas[size] = '\0';
    cputs(canvas);
}

static void
basic_check(void)
{
    struct Page *p0, *p1, *p2;
    p0 = p1 = p2 = NULL;
    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);

    assert(p0 != p1 && p0 != p2 && p1 != p2);
    assert(page_ref(p0) == 0 && page_ref(p1) == 0 && page_ref(p2) == 0);

    assert(page2pa(p0) < npage * PGSIZE);
    assert(page2pa(p1) < npage * PGSIZE);
    assert(page2pa(p2) < npage * PGSIZE);

    unsigned int num_free_store = num_free;
    num_free = 0;

    assert(alloc_page() == NULL);

    free_page(p0);
    free_page(p1);
    free_page(p2);
    assert(num_free == 3);

    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);

    assert(alloc_page() == NULL);

    free_page(p0);

    struct Page *p;
    assert((p = alloc_page()) == p0);
    assert(alloc_page() == NULL);

    assert(num_free == 0);
    num_free = num_free_store;

    free_page(p);
    free_page(p1);
    free_page(p2);
}

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

const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};
