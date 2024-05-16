#ifndef __EZFS_OPS_H__
#define __EZFS_OPS_H__

#include <linux/fs.h>

struct inode;
struct dentry;
struct file;
struct dir_context;
struct page;
struct writeback_control;
struct address_space;
struct super_block;

// Function prototypes
struct dentry *ezfs_lookup(struct inode *parent, struct dentry *child_dentry, unsigned int flags);
int ezfs_create(struct inode *parent, struct dentry *dentry, umode_t mode, bool excl);
int ezfs_unlink(struct inode *dir, struct dentry *dentry);
int ezfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode);
int ezfs_rmdir(struct inode *dir, struct dentry *dentry);
void setup_inode(struct inode *inode, struct inode *parent, umode_t mode, struct ezfs_inode *ez_inode_data, int dbn);
void update_parent_directory_times(struct inode *parent);
void update_directory_inode(struct inode *dir, bool directory_flag, struct buffer_head *inode_bh, struct ezfs_super_block *sb_data, int inode_idx, int data_blk_idx);
void ezfs_evict_inode(struct inode *inode);
int ezfs_write_inode(struct inode *inode, struct writeback_control *wbc);
int ezfs_iterate(struct file *filp, struct dir_context *ctx);
int ezfs_readpage(struct file *file, struct page *page);
int ezfs_writepage(struct page *page, struct writeback_control *wbc);
int ezfs_write_begin(struct file *file, struct address_space *mapping,
                     loff_t pos, unsigned int len, unsigned int flags,
                     struct page **pagep, void **fsdata);
int ezfs_write_end(struct file *file, struct address_space *mapping,
                   loff_t pos, unsigned len, unsigned copied,
                   struct page *page, void *fsdata);
sector_t ezfs_bmap(struct address_space *mapping, sector_t block);
static int ezfs_move_block(unsigned long base_offset, unsigned long src_offset,
                           unsigned long dest_offset, struct super_block *sb,
                           struct address_space *map);
static int ezfs_get_block(struct inode *inode, sector_t block,
                          struct buffer_head *bh_result, int create);
struct buffer_head *read_directory_block(struct super_block *sb, uint64_t block_number);
int find_free_index(uint32_t *bitmap, int max, const char *error_msg);
struct ezfs_super_block *get_ezfs_superblock(struct super_block *sb);

// File system operations structures
static const struct inode_operations ezfs_inode_ops = {
    .lookup = ezfs_lookup,
    .create = ezfs_create,
    .unlink = ezfs_unlink,
    .mkdir = ezfs_mkdir,
    .rmdir = ezfs_rmdir,
};

static const struct file_operations ezfs_dir_ops = {
    .owner = THIS_MODULE,
    .iterate_shared = ezfs_iterate,
};

static const struct file_operations ezfs_file_ops = {
    .owner = THIS_MODULE,
    .llseek = generic_file_llseek,
    .read_iter = generic_file_read_iter,
    .write_iter = generic_file_write_iter,
    .mmap = generic_file_mmap,
    .splice_read = generic_file_splice_read,
    .fsync = generic_file_fsync,
};

static const struct address_space_operations ezfs_aops = {
    .readpage = ezfs_readpage,
    .writepage = ezfs_writepage,
    .write_begin = ezfs_write_begin,
    .write_end = ezfs_write_end,
    .bmap = ezfs_bmap,
};

static struct super_operations ezfs_sb_ops = {
    .evict_inode = ezfs_evict_inode,
    .write_inode = ezfs_write_inode,
};

#endif /* __EZFS_OPS_H__ */
