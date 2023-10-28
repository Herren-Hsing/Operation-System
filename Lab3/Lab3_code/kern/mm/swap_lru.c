#include <defs.h>
#include <riscv.h>
#include <stdio.h>
#include <string.h>
#include <swap.h>
#include <swap_lru.h>
#include <list.h>

list_entry_t pra_list_head;

static int
_lru_init_mm(struct mm_struct *mm)
{
    list_init(&pra_list_head);
    mm->sm_priv = &pra_list_head;
    return 0;
}

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

static int
_lru_init(void)
{
    return 0;
}

static int
_lru_set_unswappable(struct mm_struct *mm, uintptr_t addr)
{
    return 0;
}

static int
_lru_tick_event(struct mm_struct *mm)
{
    return 0;
}

struct swap_manager swap_manager_lru =
    {
        .name = "lru swap manager",
        .init = &_lru_init,
        .init_mm = &_lru_init_mm,
        .tick_event = &_lru_tick_event,
        .map_swappable = &_lru_map_swappable,
        .set_unswappable = &_lru_set_unswappable,
        .swap_out_victim = &_lru_swap_out_victim,
        .check_swap = &_lru_check_swap,
};
