#include <defs.h>
#include <string.h>
#include <stdlib.h>
#include <list.h>
#include <stat.h>
#include <kmalloc.h>
#include <vfs.h>
#include <dev.h>
#include <sfs.h>
#include <inode.h>
#include <iobuf.h>
#include <bitmap.h>
#include <error.h>
#include <assert.h>

static const struct inode_ops sfs_node_dirops;  // dir operations
static const struct inode_ops sfs_node_fileops; // file operations

/*
 * lock_sin - lock the process of inode Rd/Wr
 */
static void
lock_sin(struct sfs_inode *sin) {
    down(&(sin->sem));
}

/*
 * unlock_sin - unlock the process of inode Rd/Wr
 */
static void
unlock_sin(struct sfs_inode *sin) {
    up(&(sin->sem));
}

/*
 * sfs_get_ops - return function addr of fs_node_dirops/sfs_node_fileops
 */
static const struct inode_ops *
sfs_get_ops(uint16_t type) {
    switch (type) {
    case SFS_TYPE_DIR:
        return &sfs_node_dirops;
    case SFS_TYPE_FILE:
        return &sfs_node_fileops;
    }
    panic("invalid file type %d.\n", type);
}

/*
 * sfs_hash_list - return inode entry in sfs->hash_list
 */
static list_entry_t *
sfs_hash_list(struct sfs_fs *sfs, uint32_t ino) {
    return sfs->hash_list + sin_hashfn(ino);
}

/*
 * sfs_set_links - link inode sin in sfs->linked-list AND sfs->hash_link
 */
static void
sfs_set_links(struct sfs_fs *sfs, struct sfs_inode *sin) {
    list_add(&(sfs->inode_list), &(sin->inode_link));
    list_add(sfs_hash_list(sfs, sin->ino), &(sin->hash_link));
}

/*
 * sfs_remove_links - unlink inode sin in sfs->linked-list AND sfs->hash_link
 */
static void
sfs_remove_links(struct sfs_inode *sin) {
    list_del(&(sin->inode_link));
    list_del(&(sin->hash_link));
}

/*
 * sfs_block_inuse - check the inode with NO. ino inuse info in bitmap
 */
static bool
sfs_block_inuse(struct sfs_fs *sfs, uint32_t ino) {
    if (ino != 0 && ino < sfs->super.blocks) {
        return !bitmap_test(sfs->freemap, ino);
    }
    panic("sfs_block_inuse: called out of range (0, %u) %u.\n", sfs->super.blocks, ino);
}

/*
 * sfs_block_alloc -  check and get a free disk block
 */
static int
sfs_block_alloc(struct sfs_fs *sfs, uint32_t *ino_store) {
    int ret;
    if ((ret = bitmap_alloc(sfs->freemap, ino_store)) != 0) {
        return ret;
    }
    assert(sfs->super.unused_blocks > 0);
    sfs->super.unused_blocks --, sfs->super_dirty = 1;
    assert(sfs_block_inuse(sfs, *ino_store));
    return sfs_clear_block(sfs, *ino_store, 1);
}

/*
 * sfs_block_free - set related bits for ino block to 1(means free) in bitmap, add sfs->super.unused_blocks, set superblock dirty *
 */
static void
sfs_block_free(struct sfs_fs *sfs, uint32_t ino) {
    assert(sfs_block_inuse(sfs, ino));
    bitmap_free(sfs->freemap, ino);
    sfs->super.unused_blocks ++, sfs->super_dirty = 1;
}

/*
 * sfs_create_inode - alloc a inode in memroy, and init din/ino/dirty/reclian_count/sem fields in sfs_inode in inode
 */
static int
sfs_create_inode(struct sfs_fs *sfs, struct sfs_disk_inode *din, uint32_t ino, struct inode **node_store) {
    struct inode *node;
    if ((node = alloc_inode(sfs_inode)) != NULL) {
        vop_init(node, sfs_get_ops(din->type), info2fs(sfs, sfs));
        struct sfs_inode *sin = vop_info(node, sfs_inode);
        sin->din = din, sin->ino = ino, sin->dirty = 0, sin->reclaim_count = 1;
        sem_init(&(sin->sem), 1);
        *node_store = node;
        return 0;
    }
    return -E_NO_MEM;
}

/*
 * lookup_sfs_nolock - according ino, find related inode
 *
 * NOTICE: le2sin, info2node MACRO
 */
static struct inode *
lookup_sfs_nolock(struct sfs_fs *sfs, uint32_t ino) {
    struct inode *node;
    list_entry_t *list = sfs_hash_list(sfs, ino), *le = list;
    while ((le = list_next(le)) != list) {
        struct sfs_inode *sin = le2sin(le, hash_link);
        if (sin->ino == ino) {
            node = info2node(sin, sfs_inode);
            if (vop_ref_inc(node) == 1) {
                sin->reclaim_count ++;
            }
            return node;
        }
    }
    return NULL;
}

/*
 * sfs_load_inode - If the inode isn't existed, load inode related ino disk block data into a new created inode.
 *                  If the inode is in memory alreadily, then do nothing
 */
int
sfs_load_inode(struct sfs_fs *sfs, struct inode **node_store, uint32_t ino) {
    lock_sfs_fs(sfs);
    struct inode *node;
    if ((node = lookup_sfs_nolock(sfs, ino)) != NULL) {
        goto out_unlock;
    }

    int ret = -E_NO_MEM;
    struct sfs_disk_inode *din;
    if ((din = kmalloc(sizeof(struct sfs_disk_inode))) == NULL) {
        goto failed_unlock;
    }

    assert(sfs_block_inuse(sfs, ino));
    if ((ret = sfs_rbuf(sfs, din, sizeof(struct sfs_disk_inode), ino, 0)) != 0) {
        goto failed_cleanup_din;
    }

    assert(din->nlinks != 0);
    if ((ret = sfs_create_inode(sfs, din, ino, &node)) != 0) {
        goto failed_cleanup_din;
    }
    sfs_set_links(sfs, vop_info(node, sfs_inode));

out_unlock:
    unlock_sfs_fs(sfs);
    *node_store = node;
    return 0;

failed_cleanup_din:
    kfree(din);
failed_unlock:
    unlock_sfs_fs(sfs);
    return ret;
}

/*
 * sfs_bmap_get_sub_nolock - 根据条目指针entp和索引，在间接磁盘块中找到磁盘块的索引。
 *                           返回间接磁盘块的索引到ino_store。无锁保护。
 * @sfs:      sfs文件系统
 * @entp:     条目磁盘块索引的指针
 * @index:    间接块中的块索引
 * @create:   布尔值，如果块未分配，如果create = 1则分配一个块，否则什么都不做
 * @ino_store: 0或者已经使用的块的索引或新分配的块的索引
 */
// 获取间接块中的子块索引（无锁版）
static int
sfs_bmap_get_sub_nolock(struct sfs_fs *sfs, uint32_t *entp, uint32_t index, bool create, uint32_t *ino_store) {
    assert(index < SFS_BLK_NENTRY); 
    int ret;
    uint32_t ent, ino = 0;
    off_t offset = index * sizeof(uint32_t);  // 子块索引在块中的偏移量

    // 如果子块索引块已存在，将子块索引块的内容读入 sfs->sfs_buffer
    if ((ent = *entp) != 0) {
        // 调用 sfs_rbuf 去读，参数：文件系统，缓冲区，读取的长度，磁盘块的编号，内容的偏移
        if ((ret = sfs_rbuf(sfs, &ino, sizeof(uint32_t), ent, offset)) != 0) {
            return ret;
        }
        // 如果块已分配或者不需要创建，则直接跳转到结束
        if (ino != 0 || !create) {
            goto out;
        }
    }
    else {
        if (!create) {
            goto out;
        }
        // 如果子块索引块不存在，分配一个子块索引块（用于间接块）
        if ((ret = sfs_block_alloc(sfs, &ent)) != 0) {
            return ret;
        }
    }
    
    // 分配一个块用于存储子块索引
    if ((ret = sfs_block_alloc(sfs, &ino)) != 0) {
        goto failed_cleanup;
    }
    // 将子块索引写入到子块索引块中的相应位置
    if ((ret = sfs_wbuf(sfs, &ino, sizeof(uint32_t), ent, offset)) != 0) {
        sfs_block_free(sfs, ino);
        goto failed_cleanup;
    }

out:
    // 更新传入的子块索引指针
    if (ent != *entp) {
        *entp = ent;
    }
    *ino_store = ino;
    return 0;

failed_cleanup:
    if (ent != *entp) {
        sfs_block_free(sfs, ent);
    }
    return ret;
}


/*
 * sfs_bmap_get_nolock - 根据 sfs_inode 和块的索引，找到磁盘块的编号
 *                       无锁保护
 * @sfs:        sfs 文件系统
 * @sin:        内存中的 sfs inode
 * @index:      inode 中的块索引
 * @create:     布尔值，如果块尚未分配，如果 create = 1 则分配一个块，否则什么都不做
 * @ino_store:  0 或者已经使用的块的索引或新分配的块的索引。
 */
// 获取块映射（块号）（无锁版）
static int
sfs_bmap_get_nolock(struct sfs_fs *sfs, struct sfs_inode *sin, uint32_t index, bool create, uint32_t *ino_store) {
    struct sfs_disk_inode *din = sin->din; // 获取磁盘索引节点
    int ret;
    uint32_t ent, ino;
	// 如果索引在前 SFS_NDIRECT 个直接块中，不创建的情况下，索引就是din->direct[index]
    // 处理直接索引
    if (index < SFS_NDIRECT) {
        // 这里先赋值ino = din->direct[index] 再判断
        // 如果块未分配且需要创建
        if ((ino = din->direct[index]) == 0 && create) {
            // 分配块
            if ((ret = sfs_block_alloc(sfs, &ino)) != 0) {
                return ret;
            }
            din->direct[index] = ino;
            sin->dirty = 1; // 标记inode已修改
        }
        // 如果不需要创建，就带着复制好的ino直接到out
        goto out;
    }
    // 如果索引在间接块中
    index -= SFS_NDIRECT; // 在间接数据块中的实际子块索引
    // SFS_BLK_NENTRY /* # of entries in a block */
    if (index < SFS_BLK_NENTRY) {
        ent = din->indirect;   
        // `indirect` 间接指向了保存文件内容数据的数据块
        // `indirect` 指向的是间接数据块（indirect block），此数据块实际存放的全部是数据块索引
        // 调用sfs_bmap_get_sub_nolock获取间接块中的子块索引
        if ((ret = sfs_bmap_get_sub_nolock(sfs, &ent, index, create, &ino)) != 0) {
            return ret;
        }
        // 更新间接块指针
        if (ent != din->indirect) {
            assert(din->indirect == 0);
            din->indirect = ent;
            sin->dirty = 1; // 标记inode已修改
        }
        goto out;
    } else {
		panic ("sfs_bmap_get_nolock - index out of range");
	}
out:
    assert(ino == 0 || sfs_block_inuse(sfs, ino)); 
    *ino_store = ino;
    return 0;
}

/*
 * sfs_bmap_free_sub_nolock - 将间接块中的索引项设为 0（表示释放）
 */
static int
sfs_bmap_free_sub_nolock(struct sfs_fs *sfs, uint32_t ent, uint32_t index) {
    assert(sfs_block_inuse(sfs, ent) && index < SFS_BLK_NENTRY);
    int ret;
    uint32_t ino, zero = 0;
    off_t offset = index * sizeof(uint32_t);

    // 通过 sfs_rbuf 读取间接块中的索引项
    if ((ret = sfs_rbuf(sfs, &ino, sizeof(uint32_t), ent, offset)) != 0) {
        return ret;
    }

    // 如果索引项不为 0
    if (ino != 0) {
        // 通过 sfs_wbuf 将索引项设为 0，表示释放
        if ((ret = sfs_wbuf(sfs, &zero, sizeof(uint32_t), ent, offset)) != 0) {
            return ret;
        }
        // 释放对应的块
        sfs_block_free(sfs, ino);
    }
    return 0;
}


/*
 * sfs_bmap_free_nolock - 释放具有逻辑索引的 inode 中的块，并重置 inode 的字段
 */
static int
sfs_bmap_free_nolock(struct sfs_fs *sfs, struct sfs_inode *sin, uint32_t index) {
    struct sfs_disk_inode *din = sin->din;
    int ret;
    uint32_t ent, ino;

    // 如果逻辑索引在直接块范围内
    if (index < SFS_NDIRECT) {
        // 如果对应的直接块不为 0
        if ((ino = din->direct[index]) != 0) {
            // 释放块
            sfs_block_free(sfs, ino);
            // 将对应直接块的索引项设为 0
            din->direct[index] = 0;
            // 标记 inode 为脏，表示发生了修改
            sin->dirty = 1;
        }
        return 0;
    }

    // 如果逻辑索引在间接块范围内
    index -= SFS_NDIRECT;
    if (index < SFS_BLK_NENTRY) {
        // 如果对应的间接块不为 0
        if ((ent = din->indirect) != 0) {
            // 将间接块中的索引项设为 0
            if ((ret = sfs_bmap_free_sub_nolock(sfs, ent, index)) != 0) {
                return ret;
            }
        }
        return 0;
    }
    return 0;
}

/*
 * sfs_bmap_load_nolock - 根据目录（DIR）的索引节点和索引节点中块的逻辑索引，找到磁盘块的编号。
 * @sfs:      sfs文件系统
 * @sin:      内存中的sfs索引节点
 * @index:    索引节点中磁盘块的逻辑索引
 * @ino_store:磁盘块的编号
 */

static int
sfs_bmap_load_nolock(struct sfs_fs *sfs, struct sfs_inode *sin, uint32_t index, uint32_t *ino_store) {
    struct sfs_disk_inode *din = sin->din; // 拿到磁盘索引
    assert(index <= din->blocks);  // 只接受 `index <= inode->blocks` 的参数
    int ret;
    uint32_t ino;
    bool create = (index == din->blocks);
    // 调用 sfs_bmap_get_nolock 具体实现功能
    // `index == inode->blocks` 时，该函数理解为需要为 `inode` 增长一个 block。并标记 `inode` 为 dirty，通过设置传入bool类型的参数 create
    if ((ret = sfs_bmap_get_nolock(sfs, sin, index, create, &ino)) != 0) {
        return ret;
    }
    assert(sfs_block_inuse(sfs, ino));
    if (create) {
        din->blocks ++;
    }
    if (ino_store != NULL) {
        *ino_store = ino;
    }
    return 0;
}

/*
 * sfs_bmap_truncate_nolock - 释放文件末尾的磁盘块
 */
static int
sfs_bmap_truncate_nolock(struct sfs_fs *sfs, struct sfs_inode *sin) {
    struct sfs_disk_inode *din = sin->din;
    assert(din->blocks != 0);
    int ret;
    // 调用 sfs_bmap_free_nolock 释放文件末尾的磁盘块
    if ((ret = sfs_bmap_free_nolock(sfs, sin, din->blocks - 1)) != 0) {
        return ret;
    }
    // 更新 din 中的块数信息
    din->blocks--;
    // 标记 inode 为脏，表示发生了修改
    sin->dirty = 1;
    return 0;
}

/*
 * sfs_dirent_read_nolock - 从包含该条目的磁盘块中读取文件条目
 * @sfs:      SFS 文件系统
 * @sin:      内存中的 SFS inode
 * @slot:     文件条目的索引
 * @entry:    文件条目结构
 */
static int
sfs_dirent_read_nolock(struct sfs_fs *sfs, struct sfs_inode *sin, int slot, struct sfs_disk_entry *entry) {
    assert(sin->din->type == SFS_TYPE_DIR && (slot >= 0 && slot < sin->din->blocks));
    int ret;
    uint32_t ino;

    // 根据目录的 inode 和文件条目的slot，找到包含该文件条目的磁盘块的索引
    if ((ret = sfs_bmap_load_nolock(sfs, sin, slot, &ino)) != 0) {
        return ret;
    }
    assert(sfs_block_inuse(sfs, ino));

    // 从磁盘块中读取文件条目的内容
    if ((ret = sfs_rbuf(sfs, entry, sizeof(struct sfs_disk_entry), ino, 0)) != 0) {
        return ret;
    }
    
    // 将文件名的末尾置为 '\0'
    entry->name[SFS_MAX_FNAME_LEN] = '\0';
    return 0;
}

#define sfs_dirent_link_nolock_check(sfs, sin, slot, lnksin, name)                  \
    do {                                                                            \
        int err;                                                                    \
        if ((err = sfs_dirent_link_nolock(sfs, sin, slot, lnksin, name)) != 0) {    \
            warn("sfs_dirent_link error: %e.\n", err);                              \
        }                                                                           \
    } while (0)

#define sfs_dirent_unlink_nolock_check(sfs, sin, slot, lnksin)                      \
    do {                                                                            \
        int err;                                                                    \
        if ((err = sfs_dirent_unlink_nolock(sfs, sin, slot, lnksin)) != 0) {        \
            warn("sfs_dirent_unlink error: %e.\n", err);                            \
        }                                                                           \
    } while (0)

/*
 * sfs_dirent_search_nolock - 在目录中查找文件条目，比较文件名与每个条目的 entry->name。
 * 如果相等，则返回条目的逻辑索引和该文件 inode 的磁盘编号。
 * @sfs:        SFS 文件系统
 * @sin:        内存中的 SFS inode
 * @name:       文件名
 * @ino_store:  存储与该文件（带有文件名）的 inode 相关的磁盘编号
 * @slot:       文件条目的逻辑索引（注意：每个文件条目占用一个磁盘块）
 * @empty_slot: 空文件条目的逻辑索引
 */
static int
sfs_dirent_search_nolock(struct sfs_fs *sfs, struct sfs_inode *sin, const char *name, uint32_t *ino_store, int *slot, int *empty_slot) {
    assert(strlen(name) <= SFS_MAX_FNAME_LEN);
    struct sfs_disk_entry *entry;

    // 为 entry 分配内存空间
    if ((entry = kmalloc(sizeof(struct sfs_disk_entry))) == NULL) {
        return -E_NO_MEM;
    }

#define set_pvalue(x, v)            do { if ((x) != NULL) { *(x) = (v); } } while (0)
    int ret, i, nslots = sin->din->blocks;
    set_pvalue(empty_slot, nslots);

    // 遍历目录中的每个文件条目
    for (i = 0; i < nslots; i++) {
        // 读取文件条目的内容
        if ((ret = sfs_dirent_read_nolock(sfs, sin, i, entry)) != 0) {
            goto out;
        }

        // 如果文件条目的 inode 为 0，则表示该条目为空
        if (entry->ino == 0) {
            set_pvalue(empty_slot, i);
            continue;
        }

        // 比较文件名是否与当前条目的文件名相等
        if (strcmp(name, entry->name) == 0) {
            // 找到相匹配的文件条目
            set_pvalue(slot, i);
            set_pvalue(ino_store, entry->ino);
            goto out;
        }
    }
#undef set_pvalue

    // 未找到匹配的文件条目
    ret = -E_NOENT;

out:
    kfree(entry);
    return ret;
}

/*
 * sfs_dirent_findino_nolock - read all file entries in DIR's inode and find a entry->ino == ino
 */

static int
sfs_dirent_findino_nolock(struct sfs_fs *sfs, struct sfs_inode *sin, uint32_t ino, struct sfs_disk_entry *entry) {
    int ret, i, nslots = sin->din->blocks;
    for (i = 0; i < nslots; i ++) {
        if ((ret = sfs_dirent_read_nolock(sfs, sin, i, entry)) != 0) {
            return ret;
        }
        if (entry->ino == ino) {
            return 0;
        }
    }
    return -E_NOENT;
}

/*
 * sfs_lookup_once - find inode corresponding the file name in DIR's sin inode 
 * @sfs:        sfs file system
 * @sin:        DIR sfs inode in memory
 * @name:       the file name in DIR
 * @node_store: the inode corresponding the file name in DIR
 * @slot:       the logical index of file entry
 */
static int
sfs_lookup_once(struct sfs_fs *sfs, struct sfs_inode *sin, const char *name, struct inode **node_store, int *slot) {
    int ret;
    uint32_t ino;
    lock_sin(sin);
    {   // find the NO. of disk block and logical index of file entry
        ret = sfs_dirent_search_nolock(sfs, sin, name, &ino, slot, NULL);
    }
    unlock_sin(sin);
    if (ret == 0) {
		// load the content of inode with the the NO. of disk block
        ret = sfs_load_inode(sfs, node_store, ino);
    }
    return ret;
}

// sfs_opendir - just check the opne_flags, now support readonly
static int
sfs_opendir(struct inode *node, uint32_t open_flags) {
    switch (open_flags & O_ACCMODE) {
    case O_RDONLY:
        break;
    case O_WRONLY:
    case O_RDWR:
    default:
        return -E_ISDIR;
    }
    if (open_flags & O_APPEND) {
        return -E_ISDIR;
    }
    return 0;
}

// sfs_openfile - open file (no use)
static int
sfs_openfile(struct inode *node, uint32_t open_flags) {
    return 0;
}

// sfs_close - close file
static int
sfs_close(struct inode *node) {
    return vop_fsync(node);
}

/*  
 * sfs_io_nolock - 从偏移位置到偏移位置+长度进行文件内容的读写
 * 磁盘块<-->缓冲区（在内存中）
 * @sfs:      SFS文件系统
 * @sin:      内存中的SFS inode
 * @buf:      用于读写的缓冲区
 * @offset:   文件的偏移量
 * @alenp:    需要读取或写入的长度（是一个指针），并且将返回实际读取或写入的长度
 * @write:    布尔值，0表示读取，1表示写入
 */
static int
sfs_io_nolock(struct sfs_fs *sfs, struct sfs_inode *sin, void *buf, off_t offset, size_t *alenp, bool write) {
    struct sfs_disk_inode *din = sin->din;
    assert(din->type != SFS_TYPE_DIR);
    off_t endpos = offset + *alenp, blkoff;
    *alenp = 0;
	// 计算Rd/Wr结束位置
    if (offset < 0 || offset >= SFS_MAX_FILE_SIZE || offset > endpos) {
        return -E_INVAL;
    }
    // 如果偏移与终止位置相同，读取/写入0字节的数据，直接返回
    if (offset == endpos) {
        return 0;
    }
    if (endpos > SFS_MAX_FILE_SIZE) {
        endpos = SFS_MAX_FILE_SIZE;
    }
    if (!write) {
        // 如果是读取数据，缓冲区中剩余的数据超出一个硬盘节点的数据大小，直接返回
        if (offset >= din->size) {
            return 0;
        }
        if (endpos > din->size) {
            endpos = din->size;
        }
    }

    int (*sfs_buf_op)(struct sfs_fs *sfs, void *buf, size_t len, uint32_t blkno, off_t offset);
    int (*sfs_block_op)(struct sfs_fs *sfs, void *buf, uint32_t blkno, uint32_t nblks);
    if (write) {
        sfs_buf_op = sfs_wbuf, sfs_block_op = sfs_wblock;
    }
    else {
        sfs_buf_op = sfs_rbuf, sfs_block_op = sfs_rblock;
    }

    int ret = 0;
    size_t size, alen = 0;
    uint32_t ino;
    uint32_t blkno = offset / SFS_BLKSIZE;          // Rd/Wr开始块的编号
    uint32_t nblks = endpos / SFS_BLKSIZE - blkno;  // Rd/Wr块的数量

    //LAB8:EXERCISE1 YOUR CODE HINT: call sfs_bmap_load_nolock, sfs_rbuf, sfs_rblock,etc. read different kind of blocks in file
// 调用sfs_bmap_load_nolock、sfs_rbuf、sfs_rblock等函数，读取文件中的不同类型的块
/*
	(1) 如果偏移量与第一个块不对齐，则从偏移量到第一个块的末尾读/写一些内容
	读/写大小 = (nblks != 0) ? (SFS_BLKSIZE - blkoff) : (endpos - offset)
	(2) 读/写对齐的块
	(3) 如果结束位置与最后一个块不对齐，则从开头到最后一个块的(endpos % SFS_BLKSIZE)处读/写一些内容
*/
    // 如果 offset 与第一个块不对齐，从offset到第一个块的末尾Rd/Wr一些内容
    // SFS_BLKSIZE 是块大小
    // blkoff 是Rd/Wr开始块的编号；nblks 是Rd/Wr块的数量
    blkoff = offset % SFS_BLKSIZE;   // 计算出在第一块数据块中进行读或写操作的偏移量
    if (blkoff != 0) {
        if(nblks == 0){ // 不满一块
            size = endpos - offset;  // 计算出在第一块数据块中进行读或写操作需要的数据长度
        }
        else{
            size = SFS_BLKSIZE - blkoff;
        }
        // blkno是开始块编号
        // 获取当前这个数据块对应到的磁盘上的数据块的编号
        if ((ret = sfs_bmap_load_nolock(sfs, sin, blkno, &ino)) != 0) {
            goto out;
        }

        // 进行读/写，利用这个sfs_buf_op分别调用sfs_wbuf或sfs_rbuf，在前面设置的
        // 将数据写入到磁盘中
        if ((ret = sfs_buf_op(sfs, buf, size, ino, blkoff)) != 0) {
            goto out;
        }

        // 维护已经读写成功的数据长度信息
        alen += size;
        buf += size; // 维护缓冲区的偏移量
        
        // 如果没有块了
        if (nblks == 0) {
            goto out;
        }
        
        blkno++;
        nblks--;
    }

    // 接下来，处理那些对齐的块，还是一样的调用
    if (nblks > 0) {
        if ((ret = sfs_bmap_load_nolock(sfs, sin, blkno, &ino)) != 0) {
            goto out;
        }
        // 将这些磁盘上的这些数据块进行读或写操作
        if ((ret = sfs_block_op(sfs, buf, ino, nblks)) != 0) {
            goto out;
        }

        alen += nblks * SFS_BLKSIZE;
        buf += nblks * SFS_BLKSIZE;
        blkno += nblks;
        nblks -= nblks;
    }

    // 如果结束位置与最后一个块不对齐，从开始到最后一个块的(endpos % SFS_BLKSIZE)Rd/Wr一些内容
    if (endpos % SFS_BLKSIZE != 0) {
        size = endpos % SFS_BLKSIZE; // 确定在这数据块中需要读写的长度
        if ((ret = sfs_bmap_load_nolock(sfs, sin, blkno, &ino)) != 0) {
            goto out;
        }
        if ((ret = sfs_buf_op(sfs, buf, size, ino, 0)) != 0) {
            goto out;
        }
        alen += size;
    }

out:
    *alenp = alen;
    if (offset + alen > sin->din->size) {
        sin->din->size = offset + alen;
        sin->dirty = 1;
    }
    return ret;
}

/*
 * sfs_io - Rd/Wr file. the wrapper of sfs_io_nolock
            with lock protect
 */
static inline int
sfs_io(struct inode *node, struct iobuf *iob, bool write) {
    struct sfs_fs *sfs = fsop_info(vop_fs(node), sfs);
    struct sfs_inode *sin = vop_info(node, sfs_inode);
    int ret;
    lock_sin(sin);
    {
        size_t alen = iob->io_resid;
        ret = sfs_io_nolock(sfs, sin, iob->io_base, iob->io_offset, &alen, write);
        if (alen != 0) {
            iobuf_skip(iob, alen);
        }
    }
    unlock_sin(sin);
    return ret;
}

// sfs_read - read file
static int
sfs_read(struct inode *node, struct iobuf *iob) {
    return sfs_io(node, iob, 0);
}

// sfs_write - write file
static int
sfs_write(struct inode *node, struct iobuf *iob) {
    return sfs_io(node, iob, 1);
}

/*
 * sfs_fstat - Return nlinks/block/size, etc. info about a file. The pointer is a pointer to struct stat;
 */
static int
sfs_fstat(struct inode *node, struct stat *stat) {
    int ret;
    memset(stat, 0, sizeof(struct stat));
    if ((ret = vop_gettype(node, &(stat->st_mode))) != 0) {
        return ret;
    }
    struct sfs_disk_inode *din = vop_info(node, sfs_inode)->din;
    stat->st_nlinks = din->nlinks;
    stat->st_blocks = din->blocks;
    stat->st_size = din->size;
    return 0;
}

/*
 * sfs_fsync - Force any dirty inode info associated with this file to stable storage.
 */
static int
sfs_fsync(struct inode *node) {
    struct sfs_fs *sfs = fsop_info(vop_fs(node), sfs);
    struct sfs_inode *sin = vop_info(node, sfs_inode);
    int ret = 0;
    if (sin->dirty) {
        lock_sin(sin);
        {
            if (sin->dirty) {
                sin->dirty = 0;
                if ((ret = sfs_wbuf(sfs, sin->din, sizeof(struct sfs_disk_inode), sin->ino, 0)) != 0) {
                    sin->dirty = 1;
                }
            }
        }
        unlock_sin(sin);
    }
    return ret;
}

/*
 *sfs_namefile -Compute pathname relative to filesystem root of the file and copy to the specified io buffer.
 *  
 */
static int
sfs_namefile(struct inode *node, struct iobuf *iob) {
    struct sfs_disk_entry *entry;
    if (iob->io_resid <= 2 || (entry = kmalloc(sizeof(struct sfs_disk_entry))) == NULL) {
        return -E_NO_MEM;
    }

    struct sfs_fs *sfs = fsop_info(vop_fs(node), sfs);
    struct sfs_inode *sin = vop_info(node, sfs_inode);

    int ret;
    char *ptr = iob->io_base + iob->io_resid;
    size_t alen, resid = iob->io_resid - 2;
    vop_ref_inc(node);
    while (1) {
        struct inode *parent;
        if ((ret = sfs_lookup_once(sfs, sin, "..", &parent, NULL)) != 0) {
            goto failed;
        }

        uint32_t ino = sin->ino;
        vop_ref_dec(node);
        if (node == parent) {
            vop_ref_dec(node);
            break;
        }

        node = parent, sin = vop_info(node, sfs_inode);
        assert(ino != sin->ino && sin->din->type == SFS_TYPE_DIR);

        lock_sin(sin);
        {
            ret = sfs_dirent_findino_nolock(sfs, sin, ino, entry);
        }
        unlock_sin(sin);

        if (ret != 0) {
            goto failed;
        }

        if ((alen = strlen(entry->name) + 1) > resid) {
            goto failed_nomem;
        }
        resid -= alen, ptr -= alen;
        memcpy(ptr, entry->name, alen - 1);
        ptr[alen - 1] = '/';
    }
    alen = iob->io_resid - resid - 2;
    ptr = memmove(iob->io_base + 1, ptr, alen);
    ptr[-1] = '/', ptr[alen] = '\0';
    iobuf_skip(iob, alen);
    kfree(entry);
    return 0;

failed_nomem:
    ret = -E_NO_MEM;
failed:
    vop_ref_dec(node);
    kfree(entry);
    return ret;
}

/*
 * sfs_getdirentry_sub_noblock - get the content of file entry in DIR
 */
static int
sfs_getdirentry_sub_nolock(struct sfs_fs *sfs, struct sfs_inode *sin, int slot, struct sfs_disk_entry *entry) {
    int ret, i, nslots = sin->din->blocks;
    for (i = 0; i < nslots; i ++) {
        if ((ret = sfs_dirent_read_nolock(sfs, sin, i, entry)) != 0) {
            return ret;
        }
        if (entry->ino != 0) {
            if (slot == 0) {
                return 0;
            }
            slot --;
        }
    }
    return -E_NOENT;
}

/*
 * sfs_getdirentry - according to the iob->io_offset, calculate the dir entry's slot in disk block,
                     get dir entry content from the disk 
 */
static int
sfs_getdirentry(struct inode *node, struct iobuf *iob) {
    struct sfs_disk_entry *entry;
    if ((entry = kmalloc(sizeof(struct sfs_disk_entry))) == NULL) {
        return -E_NO_MEM;
    }

    struct sfs_fs *sfs = fsop_info(vop_fs(node), sfs);
    struct sfs_inode *sin = vop_info(node, sfs_inode);

    int ret, slot;
    off_t offset = iob->io_offset;
    if (offset < 0 || offset % sfs_dentry_size != 0) {
        kfree(entry);
        return -E_INVAL;
    }
    if ((slot = offset / sfs_dentry_size) > sin->din->blocks) {
        kfree(entry);
        return -E_NOENT;
    }
    lock_sin(sin);
    if ((ret = sfs_getdirentry_sub_nolock(sfs, sin, slot, entry)) != 0) {
        unlock_sin(sin);
        goto out;
    }
    unlock_sin(sin);
    ret = iobuf_move(iob, entry->name, sfs_dentry_size, 1, NULL);
out:
    kfree(entry);
    return ret;
}

/*
 * sfs_reclaim - Free all resources inode occupied . Called when inode is no longer in use. 
 */
static int
sfs_reclaim(struct inode *node) {
    struct sfs_fs *sfs = fsop_info(vop_fs(node), sfs);
    struct sfs_inode *sin = vop_info(node, sfs_inode);

    int  ret = -E_BUSY;
    uint32_t ent;
    lock_sfs_fs(sfs);
    assert(sin->reclaim_count > 0);
    if ((-- sin->reclaim_count) != 0 || inode_ref_count(node) != 0) {
        goto failed_unlock;
    }
    if (sin->din->nlinks == 0) {
        if ((ret = vop_truncate(node, 0)) != 0) {
            goto failed_unlock;
        }
    }
    if (sin->dirty) {
        if ((ret = vop_fsync(node)) != 0) {
            goto failed_unlock;
        }
    }
    sfs_remove_links(sin);
    unlock_sfs_fs(sfs);

    if (sin->din->nlinks == 0) {
        sfs_block_free(sfs, sin->ino);
        if ((ent = sin->din->indirect) != 0) {
            sfs_block_free(sfs, ent);
        }
    }
    kfree(sin->din);
    vop_kill(node);
    return 0;

failed_unlock:
    unlock_sfs_fs(sfs);
    return ret;
}

/*
 * sfs_gettype - Return type of file. The values for file types are in sfs.h.
 */
static int
sfs_gettype(struct inode *node, uint32_t *type_store) {
    struct sfs_disk_inode *din = vop_info(node, sfs_inode)->din;
    switch (din->type) {
    case SFS_TYPE_DIR:
        *type_store = S_IFDIR;
        return 0;
    case SFS_TYPE_FILE:
        *type_store = S_IFREG;
        return 0;
    case SFS_TYPE_LINK:
        *type_store = S_IFLNK;
        return 0;
    }
    panic("invalid file type %d.\n", din->type);
}

/* 
 * sfs_tryseek - Check if seeking to the specified position within the file is legal.
 */
static int
sfs_tryseek(struct inode *node, off_t pos) {
    if (pos < 0 || pos >= SFS_MAX_FILE_SIZE) {
        return -E_INVAL;
    }
    struct sfs_inode *sin = vop_info(node, sfs_inode);
    if (pos > sin->din->size) {
        return vop_truncate(node, pos);
    }
    return 0;
}

/*
 * sfs_truncfile : reszie the file with new length
 */
static int
sfs_truncfile(struct inode *node, off_t len) {
    if (len < 0 || len > SFS_MAX_FILE_SIZE) {
        return -E_INVAL;
    }
    struct sfs_fs *sfs = fsop_info(vop_fs(node), sfs);
    struct sfs_inode *sin = vop_info(node, sfs_inode);
    struct sfs_disk_inode *din = sin->din;

    int ret = 0;
	//new number of disk blocks of file
    uint32_t nblks, tblks = ROUNDUP_DIV(len, SFS_BLKSIZE);
    if (din->size == len) {
        assert(tblks == din->blocks);
        return 0;
    }

    lock_sin(sin);
	// old number of disk blocks of file
    nblks = din->blocks;
    if (nblks < tblks) {
		// try to enlarge the file size by add new disk block at the end of file
        while (nblks != tblks) {
            if ((ret = sfs_bmap_load_nolock(sfs, sin, nblks, NULL)) != 0) {
                goto out_unlock;
            }
            nblks ++;
        }
    }
    else if (tblks < nblks) {
		// try to reduce the file size 
        while (tblks != nblks) {
            if ((ret = sfs_bmap_truncate_nolock(sfs, sin)) != 0) {
                goto out_unlock;
            }
            nblks --;
        }
    }
    assert(din->blocks == tblks);
    din->size = len;
    sin->dirty = 1;

out_unlock:
    unlock_sin(sin);
    return ret;
}

/*
 * sfs_lookup - Parse path relative to the passed directory
 *              DIR, and hand back the inode for the file it
 *              refers to.
 */
static int
sfs_lookup(struct inode *node, char *path, struct inode **node_store) {
    struct sfs_fs *sfs = fsop_info(vop_fs(node), sfs);
    assert(*path != '\0' && *path != '/');
    vop_ref_inc(node);
    struct sfs_inode *sin = vop_info(node, sfs_inode);
    if (sin->din->type != SFS_TYPE_DIR) {
        vop_ref_dec(node);
        return -E_NOTDIR;
    }
    struct inode *subnode;
    int ret = sfs_lookup_once(sfs, sin, path, &subnode, NULL);

    vop_ref_dec(node);
    if (ret != 0) {
        return ret;
    }
    *node_store = subnode;
    return 0;
}

// The sfs specific DIR operations correspond to the abstract operations on a inode.
static const struct inode_ops sfs_node_dirops = {
    .vop_magic                      = VOP_MAGIC,
    .vop_open                       = sfs_opendir,
    .vop_close                      = sfs_close,
    .vop_fstat                      = sfs_fstat,
    .vop_fsync                      = sfs_fsync,
    .vop_namefile                   = sfs_namefile,
    .vop_getdirentry                = sfs_getdirentry,
    .vop_reclaim                    = sfs_reclaim,
    .vop_gettype                    = sfs_gettype,
    .vop_lookup                     = sfs_lookup,
};
/// The sfs specific FILE operations correspond to the abstract operations on a inode.
static const struct inode_ops sfs_node_fileops = {
    .vop_magic                      = VOP_MAGIC,
    .vop_open                       = sfs_openfile,
    .vop_close                      = sfs_close,
    .vop_read                       = sfs_read,
    .vop_write                      = sfs_write,
    .vop_fstat                      = sfs_fstat,
    .vop_fsync                      = sfs_fsync,
    .vop_reclaim                    = sfs_reclaim,
    .vop_gettype                    = sfs_gettype,
    .vop_tryseek                    = sfs_tryseek,
    .vop_truncate                   = sfs_truncfile,
};

