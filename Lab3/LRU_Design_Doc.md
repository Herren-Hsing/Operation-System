# LRU 算法

> Contributors：焦心雨(2112536)、李艺楠(2110246)、辛浩然(2112514)
>
> [GitHub链接](https://github.com/Herren-Hsing/Operation-System)

LRU (Least Recently Used) 页面置换算法的基本思路是选择最长时间没有被引用的页面进行置换。

我们的实现思路是维护一个按最近一次访问时间排序的页面链表，链表首节点是最近刚刚使用过的页面，而链表尾节点是最久未使用的页面。

每当访问一个页面时，如果不存在于链表中，将其添加至链表首；如果该页面已存在于链表中，则将其移到链表首；

当缺页时，置换链表尾节点的页面。

接下来具体分析算法的实现。

## 算法实现

首先，在`_lru_init_mm`中，初始化按最近一次访问时间排序的双向的页面链表。

```c
static int
_lru_init_mm(struct mm_struct *mm)
{
    list_init(&pra_list_head);
    mm->sm_priv = &pra_list_head;
    return 0;
}
```

当需要访问某虚拟地址对应的页时，我们提供了访问页的接口函数`read_page`和`write_page`。

`write_page`用于向指定的虚拟地址写入指定的数据。

- 首先，使用 `get_pte` 函数获取给定内存地址 `a` 的页表项(Page Table Entry，通常用于虚拟内存管理)。

- 检查页表项是否存在且标志 PTE_V(有效位)是否已设置。如果页表项存在且有效，表示这个地址已经映射到物理内存中。
- 如果地址有效，将页表项中的信息转换为一个指向物理页面的结构体指针 `page`。
- 调用 `_lru_map_swappable` 函数，将页面标记为可交换(swappable)，并且修改页面链表。

`read_page`用于读取指定的虚拟地址存放的数据。实现方式同上。

```c
static void write_page(struct mm_struct *mm, unsigned char *a, unsigned char b)
{
    pte_t *ptep = get_pte(mm->pgdir, (uintptr_t)a, 0);
    // 没有异常时，访问需要加链
    if ((ptep != NULL) && (*ptep & PTE_V))
    {
        struct Page *page = pte2page(*ptep);
        _lru_map_swappable(mm, (uintptr_t)a, page, 1);
    }
    *a = b;
}

static unsigned char read_page(struct mm_struct *mm, unsigned char *a)
{
    pte_t *ptep = get_pte(mm->pgdir, (uintptr_t)a, 0);
    if ((ptep != NULL) && (*ptep & PTE_V))
    {
        struct Page *page = pte2page(*ptep);
        _lru_map_swappable(mm, (uintptr_t)a, page, 1);
    }
    return *(unsigned char *)a;
}
```

接下来，编写`_lru_map_swappable`来管理待替换页的链表。

每当访问一个页，需要将该页加入链表时，首先遍历链表，删除链表中的所有该页结构体(如果存在)，然后将该页的页结构体放入链表头。

```c
static int
_lru_map_swappable(struct mm_struct *mm, uintptr_t addr, struct Page *page, int swap_in)
{
    list_entry_t *head = (list_entry_t *)mm->sm_priv;
    list_entry_t *entry = &(page->pra_page_link);
    assert(entry != NULL && head != NULL);
    list_entry_t *tmp = head->next;

    while (tmp != head)
    {
        if (tmp == entry)
        {
            tmp = tmp->next;
            list_del(tmp->prev);
        }
        else
        {
            tmp = tmp->next;
        }
    }

    list_add(head, entry);
    return 0;
}
```

当需要调用`swap out`换出某页时，会调用`_lru_swap_out_victim`函数选择被替换的页。根据我们的LRU算法，会替换掉链表尾的页。

实现方法就是获取链表中的最后一个页面，从链表中删除这个页面，然后使用 `le2page` 宏得到该页面结构体，将页面结构体指针赋值给参数 `ptr_page`。

```c
static int
_lru_swap_out_victim(struct mm_struct *mm, struct Page **ptr_page, int in_tick)
{
    list_entry_t *head = (list_entry_t *)mm->sm_priv;
    assert(head != NULL);
    assert(in_tick == 0);

    list_entry_t *entry = list_prev(head);
    if (entry != head)
    {
        list_del(entry);
        *ptr_page = le2page(entry, pra_page_link);
    }
    else
    {
        *ptr_page = NULL;
    }
    return 0;
}
```

## 算法测试

在ucore中，关于页面置换算法的测试，首先调用`check_swap`函数，进行了创建vma、建立页表、分配物理页、创建映射等操作。经过这些操作后，页表中存在虚拟地址0x1000、0x2000、0x30000和x4000的映射关系，且内存中最多能放四个页，也就是当访问第五个页时，如果发生缺页异常，会涉及到页面的换入和换出。 

然后调用所指定的交换管理器的验证函数。

在`_lru_check_swap`函数中，我们编写了测试用例：

```c
static int
_lru_check_swap(void)
{
    cprintf("write Virt Page c in lru_check_swap\n");
    write_page(check_mm_struct, (unsigned char *)0x3000, 0x0c);
    assert(pgfault_num == 4);
    cprintf("write Virt Page a in lru_check_swap\n");
    write_page(check_mm_struct, (unsigned char *)0x1000, 0x0a);
    assert(pgfault_num == 4);
    cprintf("write Virt Page d in lru_check_swap\n");
    write_page(check_mm_struct, (unsigned char *)0x4000, 0x0d);
    assert(pgfault_num == 4);
    cprintf("write Virt Page b in lru_check_swap\n");
    write_page(check_mm_struct, (unsigned char *)0x2000, 0x0b);
    assert(pgfault_num == 4);
    cprintf("write Virt Page e in lru_check_swap\n");
    write_page(check_mm_struct, (unsigned char *)0x5000, 0x0e);
    assert(pgfault_num == 5); // 替换3
    cprintf("write Virt Page b in lru_check_swap\n");
    write_page(check_mm_struct, (unsigned char *)0x2000, 0x0b);
    assert(pgfault_num == 5);
    cprintf("write Virt Page a in lru_check_swap\n");
    write_page(check_mm_struct, (unsigned char *)0x1000, 0x0a);
    assert(pgfault_num == 5);
    cprintf("write Virt Page c in lru_check_swap\n");
    write_page(check_mm_struct, (unsigned char *)0x3000, 0x0c);
    assert(pgfault_num == 6); // 替换4
    cprintf("write Virt Page d in lru_check_swap\n");
    write_page(check_mm_struct, (unsigned char *)0x4000, 0x0d);
    assert(pgfault_num == 7); // 替换5
    cprintf("write Virt Page b in lru_check_swap\n");
    write_page(check_mm_struct, (unsigned char *)0x2000, 0x0b);
    assert(pgfault_num == 7);
    cprintf("write Virt Page e in lru_check_swap\n");
    write_page(check_mm_struct, (unsigned char *)0x5000, 0x0e);
    assert(pgfault_num == 8); // 替换 1
    cprintf("write Virt Page a in lru_check_swap\n");
    write_page(check_mm_struct, (unsigned char *)0x1000, 0x0a);
    assert(pgfault_num == 9); // 替换3

    assert(read_page(check_mm_struct, (unsigned char *)0x3000) == 0x0c);
    assert(pgfault_num == 10); // 替换4
    assert(read_page(check_mm_struct, (unsigned char *)0x2000) == 0x0b);
    assert(pgfault_num == 10);
    assert(read_page(check_mm_struct, (unsigned char *)0x4000) == 0x0d);
    assert(pgfault_num == 11); // 替换5
    
    cprintf("write Virt Page a in lru_check_swap\n");
    write_page(check_mm_struct, (unsigned char *)0x1000, 0x0a);
    assert(pgfault_num == 11);
    return 0;
}
```

针对编写的访问流程，我们编写表格， 列举缺页情况和链表情况：

- 表中的链表状态为每次页替换之后的链表状态
- 初始缺页总次数是4，因为在`check_swap`函数中已有4次缺页
- 为方便列表，我们记虚拟地址0x1000对应的物理页为A，0x2000为B，0x3000为C，0x4000为D，0x5000为E


![image-20231025220505193.png](https://s2.loli.net/2023/10/25/GzFwNOJ86UBbHZx.png)

我们在`swap_init`中将交换管理器修改为所编写的LRU算法管理器(` sm = &swap_manager_lru;`)。

然后`make qemu`，终端输出如下结果(只列出与LRU测试有关的输出)：

```
......
set up init env for check_swap begin!
Store/AMO page fault
page fault at 0x00001000: K/W
Store/AMO page fault
page fault at 0x00002000: K/W
Store/AMO page fault
page fault at 0x00003000: K/W
Store/AMO page fault
page fault at 0x00004000: K/W
set up init env for check_swap over!
write Virt Page c in lru_check_swap
write Virt Page a in lru_check_swap
write Virt Page d in lru_check_swap
write Virt Page b in lru_check_swap
write Virt Page e in lru_check_swap
Store/AMO page fault
page fault at 0x00005000: K/W
swap_out: i 0, store page in vaddr 0x3000 to disk swap entry 4
write Virt Page b in lru_check_swap
write Virt Page a in lru_check_swap
write Virt Page c in lru_check_swap
Store/AMO page fault
page fault at 0x00003000: K/W
swap_out: i 0, store page in vaddr 0x4000 to disk swap entry 5
swap_in: load disk swap entry 4 with swap_page in vadr 0x3000
write Virt Page d in lru_check_swap
Store/AMO page fault
page fault at 0x00004000: K/W
swap_out: i 0, store page in vaddr 0x5000 to disk swap entry 6
swap_in: load disk swap entry 5 with swap_page in vadr 0x4000
write Virt Page b in lru_check_swap
write Virt Page e in lru_check_swap
Store/AMO page fault
page fault at 0x00005000: K/W
swap_out: i 0, store page in vaddr 0x1000 to disk swap entry 2
swap_in: load disk swap entry 6 with swap_page in vadr 0x5000
write Virt Page a in lru_check_swap
Store/AMO page fault
page fault at 0x00001000: K/W
swap_out: i 0, store page in vaddr 0x3000 to disk swap entry 4
swap_in: load disk swap entry 2 with swap_page in vadr 0x1000
Load page fault
page fault at 0x00003000: K/R
swap_out: i 0, store page in vaddr 0x4000 to disk swap entry 5
swap_in: load disk swap entry 4 with swap_page in vadr 0x3000
Load page fault
page fault at 0x00004000: K/R
swap_out: i 0, store page in vaddr 0x5000 to disk swap entry 6
swap_in: load disk swap entry 5 with swap_page in vadr 0x4000
write Virt Page a in lru_check_swap
count is 1, total is 8
check_swap() succeeded!
++ setup timer interrupts
100 ticks
......
```

可见，我们的LRU算法能够进行预期的页面置换。
