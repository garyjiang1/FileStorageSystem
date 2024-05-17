#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/writeback.h>
#include <linux/fs_context.h>
#include <linux/pagemap.h>
#include <linux/printk.h>
#include <linux/kernel.h>
#include "fileStorage.h"
#include "fileStorageOperations.h"

static inline struct buffer_head *
check_buffer_head(struct buffer_head *bh, const char *msg)
{
	if (!bh) {
		pr_warn("EZFS: %s is null.\n", msg);
		return NULL;
	}

	return bh;
}

struct buffer_head *
read_directory_block(struct super_block *sb, uint64_t block_number)
{
	struct buffer_head *bh = sb_bread(sb, block_number);

	if (!bh) {
		pr_err("Failed to read block\n");
		return ERR_PTR(-EIO);
	}
	return bh;
}

int
find_free_index(uint32_t *bitmap, int max, const char *error_msg)
{
	int idx;

	for (idx = 0; idx < max; idx++) {
		if (!IS_SET(bitmap, idx))
			return idx;
	}

	pr_err("%s\n", error_msg);
	return -ENOSPC;
}

struct ezfs_super_block *
get_ezfs_superblock(struct super_block *sb)
{
	struct ezfs_sb_buffer_heads *sb_heads = sb->s_fs_info;
	struct buffer_head *sb_buffer_head =
	    check_buffer_head(sb_heads ? sb_heads->sb_bh : NULL,
			      "Superblock buffer head");
	return (struct ezfs_super_block *) check_buffer_head(sb_buffer_head,
							     "Superblock buffer")->
	    b_data;
}

static void
write_inode_helper(struct inode *inode, struct ezfs_inode *ezfs_inode)
{
	ezfs_inode->mode = inode->i_mode;
	ezfs_inode->file_size = inode->i_size;
	ezfs_inode->nlink = inode->i_nlink;
	ezfs_inode->i_atime = inode->i_atime;
	ezfs_inode->i_mtime = inode->i_mtime;
	ezfs_inode->i_ctime = inode->i_ctime;
	ezfs_inode->uid = inode->i_uid.val;
	ezfs_inode->gid = inode->i_gid.val;
	ezfs_inode->nblocks = inode->i_blocks / 8;
}

int
ezfs_readpage(struct file *file_handle, struct page *page_obj)
{
	pr_debug("EZFS: Reading page from file %pD\n", file_handle);

	return block_read_full_page(page_obj, ezfs_get_block);
}

int
ezfs_writepage(struct page *target_page, struct writeback_control *wb_ctrl)
{
	pr_debug("EZFS: Writing page to disk\n");

	return block_write_full_page(target_page, ezfs_get_block, wb_ctrl);
}

static void
handle_write_failure(struct address_space *space, loff_t end_pos)
{
	if (end_pos > space->host->i_size) {
		pr_warn("EZFS: Truncating page cache beyond current size\n");
		truncate_pagecache(space->host, space->host->i_size);
	}
}

sector_t
ezfs_bmap(struct address_space *map_space, sector_t blk)
{
	pr_debug("EZFS: Mapping block %llu in address space\n", blk);
	return generic_block_bmap(map_space, blk, ezfs_get_block);
}

void
setup_inode(struct inode *inode, struct inode *parent, umode_t mode,
	    struct ezfs_inode *ez_inode_data, int dbn)
{
	inode->i_mode = mode;
	inode->i_uid = current_fsuid();
	inode->i_gid = current_fsgid();
	inode->i_blocks = 0;
	inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
	inode->i_private = ez_inode_data;
	ez_inode_data->dbn = dbn;
}

static struct buffer_head *
get_ezfs_buffer_head(struct super_block *sb)
{
	struct ezfs_sb_buffer_heads *sb_heads = sb->s_fs_info;

	return check_buffer_head(sb_heads ? sb_heads->i_store_bh : NULL,
				 "Inode store buffer head");
}

static struct ezfs_inode *
get_ezfs_inode(struct inode *inode)
{
	return (struct ezfs_inode *) check_buffer_head(inode ? inode->i_private : NULL,
												"Inode private data");
}

void
update_parent_directory_times(struct inode *parent)
{
	parent->i_mtime = parent->i_ctime = current_time(parent);

	mark_inode_dirty(parent);
}

void
update_directory_inode(struct inode *dir, bool directory_flag,
		       struct buffer_head *inode_bh,
		       struct ezfs_super_block *sb_data, int inode_idx,
		       int data_blk_idx)
{
	if (directory_flag)
		inc_nlink(dir);

	dir->i_size += sizeof(struct ezfs_dir_entry);
	mark_buffer_dirty(inode_bh);
}

static void
update_inode_metadata(struct inode *inode, struct inode *dir)
{
	inode->i_ctime = dir->i_ctime = dir->i_mtime = current_time(inode);
	drop_nlink(inode);
	mark_inode_dirty(inode);
	mark_inode_dirty(dir);
}

void
release_inode_resources(struct ezfs_super_block *ezfs_sb,
			struct ezfs_inode *ezfs_inode, int blocks)
{
	int data_blk_num = ezfs_inode->dbn;
	int i;

	for (i = 0; i < blocks; i++) {
		CLEARBIT(ezfs_sb->free_data_blocks,
			 data_blk_num - EZFS_ROOT_DATABLOCK_NUMBER + i);
	}
}

void
ezfs_evict_inode(struct inode *inode)
{
	struct ezfs_sb_buffer_heads *sb_heads = inode->i_sb->s_fs_info;
	struct buffer_head *sb_bh =
	    check_buffer_head(sb_heads ? sb_heads->sb_bh : NULL,
			      "Superblock buffer head");
	struct ezfs_super_block *ezfs_sb = get_ezfs_superblock(inode->i_sb);
	struct ezfs_inode *ezfs_inode = get_ezfs_inode(inode);
	int blocks = inode->i_blocks / 8;

	if (!inode->i_nlink) {
		CLEARBIT(ezfs_sb->free_inodes,
			 inode->i_ino - EZFS_ROOT_INODE_NUMBER);
		release_inode_resources(ezfs_sb, ezfs_inode, blocks);
		mark_buffer_dirty(sb_bh);
	}

	truncate_inode_pages_final(&inode->i_data);
	clear_inode(inode);
}

int
ezfs_update_inode_from_vfs(struct ezfs_inode *ez_inode, struct inode *vfs_inode)
{
	ez_inode->mode = vfs_inode->i_mode;
	ez_inode->file_size = vfs_inode->i_size;
	ez_inode->nlink = vfs_inode->i_nlink;
	ez_inode->i_atime = vfs_inode->i_atime;
	ez_inode->i_mtime = vfs_inode->i_mtime;
	ez_inode->i_ctime = vfs_inode->i_ctime;
	ez_inode->uid = vfs_inode->i_uid.val;
	ez_inode->gid = vfs_inode->i_gid.val;
	ez_inode->nblocks = vfs_inode->i_blocks / 8;

	return 0;
}

static struct inode *
ezfs_iget(struct super_block *sb, int inode_number)
{
	struct inode *vfs_inode = iget_locked(sb, inode_number);

	if (vfs_inode && (vfs_inode->i_state & I_NEW)) {
		struct ezfs_inode *internal_inode =
		    (struct ezfs_inode *) get_ezfs_buffer_head(sb)->b_data +
		    inode_number - EZFS_ROOT_INODE_NUMBER;

		vfs_inode->i_private = internal_inode;
		vfs_inode->i_mode = internal_inode->mode;
		vfs_inode->i_op = &ezfs_inode_ops;
		vfs_inode->i_sb = sb;
		vfs_inode->i_fop =
			(vfs_inode->i_mode & S_IFDIR) ? &ezfs_dir_ops : &ezfs_file_ops;
		vfs_inode->i_mapping->a_ops = &ezfs_aops;
		vfs_inode->i_size = internal_inode->file_size;
		vfs_inode->i_blocks = internal_inode->nblocks * 8;
		set_nlink(vfs_inode, internal_inode->nlink);
		vfs_inode->i_atime = internal_inode->i_atime;
		vfs_inode->i_mtime = internal_inode->i_mtime;
		vfs_inode->i_ctime = internal_inode->i_ctime;
		i_uid_write(vfs_inode, internal_inode->uid);
		i_gid_write(vfs_inode, internal_inode->gid);
		unlock_new_inode(vfs_inode);
	}

	return vfs_inode;
}

static int
ezfs_move_block(unsigned long base_offset, unsigned long src_offset,
		unsigned long dest_offset, struct super_block *sb,
		struct address_space *map)
{
	struct buffer_head *src_bh, *dest_bh;
	struct page *src_page;
	void *src_data;

	src_offset += EZFS_ROOT_DATABLOCK_NUMBER;
	dest_offset += EZFS_ROOT_DATABLOCK_NUMBER;

	dest_bh = sb_getblk(sb, dest_offset);
	if (!dest_bh)
		return -EIO;

	src_page = find_get_page(map, src_offset - base_offset);
	if (src_page) {
		src_data = kmap_atomic(src_page);
		memcpy(dest_bh->b_data, src_data, dest_bh->b_size);
		kunmap_atomic(src_data);
		put_page(src_page);
	} else {
		src_bh = sb_bread(sb, src_offset);
		if (!src_bh) {
			brelse(dest_bh);
			return -EIO;
		}

		memcpy(dest_bh->b_data, src_bh->b_data, src_bh->b_size);
		brelse(src_bh);

	}

	mark_buffer_dirty(dest_bh);
	brelse(dest_bh);

	pr_info("EZFS: Moved block from %lu to %lu\n", src_offset, dest_offset);

	return 0;
}

static int
ezfs_get_block(struct inode *inode, sector_t block,
	       struct buffer_head *bh_result, int create)
{
	struct super_block *sb = inode->i_sb;
	struct ezfs_sb_buffer_heads *sb_heads = sb->s_fs_info;
	struct buffer_head *sb_buffer_head =
	    check_buffer_head(sb_heads ? sb_heads->sb_bh : NULL,
			      "Superblock buffer head");
	struct ezfs_super_block *sb_data =
	    (struct ezfs_super_block *) check_buffer_head(sb_buffer_head,
							  "Superblock buffer")->
	    b_data;
	struct ezfs_inode *inode_data =
		(struct ezfs_inode *) check_buffer_head(inode ? inode->i_private : NULL,
												"Inode private data");

	unsigned long *bitmap = (unsigned long *) sb_data->free_data_blocks;
	int status = 0, physical_addr =
	    0, idx, current_block_no, total_blocks, start_index;
	int expanded_block_count, block_idx, searching_block, new_start_index;
	int i;

	current_block_no = inode_data->dbn;
	total_blocks = inode->i_blocks / 8;

	if (total_blocks)
		physical_addr = current_block_no + block;

	if (total_blocks && block < total_blocks) {
		map_bh(bh_result, sb, physical_addr);
		return 0;
	}

	if (!create)
		return 0;

	current_block_no = inode_data->dbn;
	total_blocks = inode->i_blocks / 8;
	if (total_blocks)
		physical_addr = current_block_no + block;

	if (total_blocks && block < total_blocks) {
		map_bh(bh_result, sb, physical_addr);
		return 0;
	}

	if (!create)
		return 0;

	if (physical_addr >= EZFS_ROOT_DATABLOCK_NUMBER + EZFS_MAX_DATA_BLKS)
		return -ENOSPC;

	mutex_lock(sb_data->ezfs_lock);

	idx = find_first_zero_bit(bitmap, EZFS_MAX_DATA_BLKS);
	if (idx == EZFS_MAX_DATA_BLKS) {
		mutex_unlock(sb_data->ezfs_lock);
		return -ENOSPC;
	}

	if (!total_blocks) {
		physical_addr = idx + EZFS_ROOT_DATABLOCK_NUMBER;
		inode_data->dbn = physical_addr;
		goto allocation_success;
	}

	if (!test_bit(physical_addr - EZFS_ROOT_DATABLOCK_NUMBER, bitmap))
		goto allocation_success;

	expanded_block_count = total_blocks + 1;
	for (idx = 0, searching_block = 0;
	     searching_block < expanded_block_count && idx < EZFS_MAX_DATA_BLKS;
	     idx++, searching_block++) {
		block_idx = idx + EZFS_ROOT_DATABLOCK_NUMBER;
		if (test_bit(idx, bitmap) &&
		    (current_block_no > block_idx
		     || current_block_no + total_blocks - 1 < block_idx)) {
			searching_block = -1;
		}
	}

	if (searching_block < expanded_block_count) {
		status = -ENOSPC;
		goto unlock_and_exit;
	}

	new_start_index = idx - expanded_block_count;
	physical_addr = idx - 1 + EZFS_ROOT_DATABLOCK_NUMBER;
	start_index = current_block_no - EZFS_ROOT_DATABLOCK_NUMBER;
	for (i = 0; i < total_blocks; i++) {
		ezfs_move_block(start_index, start_index + i,
				new_start_index + i, sb, inode->i_mapping);
		clear_bit(start_index + i, bitmap);
	}
	for (i = 0; i < total_blocks; i++)
		set_bit(new_start_index + i, bitmap);
	inode_data->dbn = new_start_index + EZFS_ROOT_DATABLOCK_NUMBER;

allocation_success:
	map_bh(bh_result, sb, physical_addr);

	set_bit(physical_addr - EZFS_ROOT_DATABLOCK_NUMBER, bitmap);
	mark_buffer_dirty(sb_buffer_head);

unlock_and_exit:
	mutex_unlock(sb_data->ezfs_lock);
	return status;
}

int
ezfs_iterate(struct file *file, struct dir_context *context)
{
	struct inode *inode = file_inode(file);
	uint64_t block_number =
	    ((struct ezfs_inode *)
	     check_buffer_head(inode ? inode->i_private : NULL,
			       "Inode private data"))->dbn;
	struct buffer_head *buffer_head = sb_bread(inode->i_sb, block_number);
	struct ezfs_dir_entry *entry_ptr;
	int entry_index, total_entries = EZFS_MAX_CHILDREN;

	if (!dir_emit_dots(file, context))
		return 0;

	if (!buffer_head) {
		pr_warn("EZFS: Failed to read directory block %llu\n", block_number);
		return -EIO;
	}

	entry_ptr =
	    (struct ezfs_dir_entry *) (buffer_head->b_data) + (context->pos -
							       2);

	for (entry_index = context->pos - 2; entry_index < total_entries;
	     ++entry_index, ++entry_ptr, ++context->pos) {
		if (entry_ptr->active) {
			if (!dir_emit
			    (context, entry_ptr->filename,
			     strlen(entry_ptr->filename), entry_ptr->inode_no,
			     DT_UNKNOWN)) {
				break;
			}
		}
	}

	brelse(buffer_head);

	return 0;
}

int
ezfs_write_begin(struct file *file_desc, struct address_space *space,
		 loff_t start_pos, unsigned int length,
		 unsigned int write_flags, struct page **page_handle,
		 void **fs_data)
{
	int op_result;

	op_result =
	    block_write_begin(space, start_pos, length, write_flags,
			      page_handle, ezfs_get_block);

	if (unlikely(op_result))
		handle_write_failure(space, start_pos + length);

	return op_result;
}

int ezfs_write_end(struct file *file_handle, struct address_space *space,
	loff_t start_pos, unsigned int length, unsigned int written_len,
	struct page *page_obj, void *fs_data)
{
	int final_result;
	struct inode *node = space->host;
	loff_t prev_size = node->i_size;

	final_result =
	    generic_write_end(file_handle, space, start_pos, length,
			      written_len, page_obj, fs_data);

	if (prev_size != node->i_size) {
		int old_block_count =
		    (prev_size + EZFS_BLOCK_SIZE - 1) / EZFS_BLOCK_SIZE;
		int new_block_count =
		    (node->i_size + EZFS_BLOCK_SIZE - 1) / EZFS_BLOCK_SIZE;

		node->i_blocks = new_block_count * 8;
		mark_inode_dirty(node);

		if (old_block_count > new_block_count) {
			struct ezfs_super_block *sb_info = get_ezfs_superblock(node->i_sb);
			struct ezfs_inode *inode_data = (struct ezfs_inode *) check_buffer_head(node ? node->i_private : NULL, "Inode private data");
			int idx, data_block_no = inode_data->dbn;


			mutex_lock(sb_info->ezfs_lock);

			for (idx = new_block_count; idx < old_block_count;
			     ++idx) {
				CLEARBIT(sb_info->free_data_blocks,
					 data_block_no + idx -
					 EZFS_ROOT_DATABLOCK_NUMBER);
			}
			mutex_unlock(sb_info->ezfs_lock);
		}
	}
	return final_result;
}

struct dentry *
ezfs_lookup(struct inode *directory, struct dentry *child_entry,
	    unsigned int search_flags)
{
	struct ezfs_dir_entry *dir_entry;
	struct buffer_head *buffer_head;
	struct inode *found_inode = NULL;
	uint64_t directory_block;
	int index;

	directory_block = get_ezfs_inode(directory)->dbn;
	buffer_head = sb_bread(directory->i_sb, directory_block);

	if (!buffer_head)
		return ERR_PTR(-EIO);

	dir_entry = (struct ezfs_dir_entry *) buffer_head->b_data;
	for (index = 0; index < EZFS_MAX_CHILDREN; index++, dir_entry++) {
		if (dir_entry->active
		    && strncmp(dir_entry->filename, child_entry->d_name.name,
			       child_entry->d_name.len) == 0
		    && dir_entry->filename[child_entry->d_name.len] == '\0') {
			found_inode =
			    ezfs_iget(directory->i_sb, dir_entry->inode_no);
			break;
		}
	}

	brelse(buffer_head);
	return d_splice_alias(found_inode, child_entry);
}

static struct inode *
create_inode_helper(struct inode *dir, struct dentry *dentry, umode_t mode,
		    bool isdir)
{
	int i, i_idx, d_idx, i_num, d_num;
	struct ezfs_super_block *ezfs_sb = get_ezfs_superblock(dir->i_sb);
	struct buffer_head *dir_bh, *i_bh;
	struct ezfs_dir_entry *ezfs_dentry;
	struct inode *new_inode, *ret = NULL;
	struct ezfs_inode *new_ezfs_inode;
	uint64_t dir_blk_num = get_ezfs_inode(dir)->dbn;
struct ezfs_sb_buffer_heads *sb_buf_heads;

	if (strnlen(dentry->d_name.name, EZFS_MAX_FILENAME_LENGTH + 1) >
	    EZFS_MAX_FILENAME_LENGTH) {
		return ERR_PTR(-ENAMETOOLONG);
	}

	dir_bh = read_directory_block(dir->i_sb, dir_blk_num);
	if (IS_ERR(dir_bh))
		return ERR_CAST(dir_bh);

	ezfs_dentry = (struct ezfs_dir_entry *) dir_bh->b_data;

	for (i = 0; i < EZFS_MAX_CHILDREN && ezfs_dentry->active;
	    ++i, ++ezfs_dentry)
		;

	if (i == EZFS_MAX_CHILDREN) {
		brelse(dir_bh);
		return ERR_PTR(-ENOSPC);
	}

	mutex_lock(ezfs_sb->ezfs_lock);
	i_idx =
	    find_free_index(ezfs_sb->free_inodes, EZFS_MAX_INODES,
			    "No free inodes");
	if (i_idx < 0) {
		ret = ERR_PTR(i_idx);
		goto out;
	}
	i_num = i_idx + EZFS_ROOT_INODE_NUMBER;

	if (isdir)
		mode |= S_IFDIR;

	if (mode & S_IFDIR) {
		struct buffer_head *new_dir_bh;

		d_idx =
		    find_free_index(ezfs_sb->free_data_blocks,
				    EZFS_MAX_DATA_BLKS, "No free data blocks");
		if (d_idx < 0) {
			ret = ERR_PTR(d_idx);
			goto out;
		}
		d_num = d_idx + EZFS_ROOT_DATABLOCK_NUMBER;
		new_dir_bh = read_directory_block(dir->i_sb, d_num);
		if (IS_ERR(new_dir_bh)) {
			ret = ERR_CAST(new_dir_bh);
			goto out;
		}
		memset(new_dir_bh->b_data, 0, EZFS_BLOCK_SIZE);
		mark_buffer_dirty(new_dir_bh);
		brelse(new_dir_bh);
	}

	new_inode = iget_locked(dir->i_sb, i_num);
	if (IS_ERR(new_inode)) {
		ret = ERR_CAST(new_inode);
		goto out;
	}

	i_bh = get_ezfs_buffer_head(dir->i_sb);
	new_ezfs_inode = ((struct ezfs_inode *) i_bh->b_data) + i_idx;
	new_inode->i_mode = mode;
	new_inode->i_op = &ezfs_inode_ops;
	new_inode->i_sb = dir->i_sb;
	if (mode & S_IFDIR) {
		new_inode->i_fop = &ezfs_dir_ops;
		new_inode->i_size = EZFS_BLOCK_SIZE;
		new_inode->i_blocks = 8;
		new_ezfs_inode->dbn = d_num;
		set_nlink(new_inode, 2);
	} else {
		new_inode->i_fop = &ezfs_file_ops;
		new_inode->i_size = 0;
		new_inode->i_blocks = 0;
		new_ezfs_inode->dbn = -1;
		set_nlink(new_inode, 1);
	}
	new_inode->i_mapping->a_ops = &ezfs_aops;
	new_inode->i_atime = new_inode->i_mtime = new_inode->i_ctime =
	    current_time(new_inode);
	inode_init_owner(new_inode, dir, mode);

	write_inode_helper(new_inode, new_ezfs_inode);
	mark_buffer_dirty(i_bh);
	new_inode->i_private = (void *) new_ezfs_inode;

	d_instantiate_new(dentry, new_inode);
	mark_inode_dirty(new_inode);

	strncpy(ezfs_dentry->filename, dentry->d_name.name,
		strlen(dentry->d_name.name));
	ezfs_dentry->active = 1;
	ezfs_dentry->inode_no = i_num;
	mark_buffer_dirty(dir_bh);

	dir->i_mtime = dir->i_ctime = current_time(dir);
	if (mode & S_IFDIR)
		inc_nlink(dir);
	mark_inode_dirty(dir);

	SETBIT(ezfs_sb->free_inodes, i_idx);
	if (mode & S_IFDIR)
		SETBIT(ezfs_sb->free_data_blocks, d_idx);
	sb_buf_heads = dir->i_sb->s_fs_info;
	mark_buffer_dirty(sb_buf_heads->sb_bh);

out:
	brelse(dir_bh);
	mutex_unlock(ezfs_sb->ezfs_lock);
	return ret;
}

int
ezfs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
	struct inode *inode;

	inode = create_inode_helper(dir, dentry, mode, false);

	if (IS_ERR(inode))
		return PTR_ERR(inode);

	return 0;
}

static int
deactivate_dir_entry(struct buffer_head *bh, const char *filename)
{
	struct ezfs_dir_entry *ezfs_dentry =
	    (struct ezfs_dir_entry *) bh->b_data;
	int i;

	for (i = 0; i < EZFS_MAX_CHILDREN; i++) {
		if (ezfs_dentry[i].active
		    && strcmp(ezfs_dentry[i].filename, filename) == 0) {
			memset(&ezfs_dentry[i], 0,
			       sizeof(struct ezfs_dir_entry));
			mark_buffer_dirty(bh);
			return 1;
		}
	}
	return 0;
}

int
ezfs_unlink(struct inode *dir, struct dentry *dentry)
{
	int result;
	uint64_t dir_blk_num = get_ezfs_inode(dir)->dbn;
	struct buffer_head *bh = sb_bread(dir->i_sb, dir_blk_num);

	if (!bh)
		return -EIO;

	result = deactivate_dir_entry(bh, dentry->d_name.name);
	if (result)
		update_inode_metadata(d_inode(dentry), dir);

	brelse(bh);

	return result ? 0 : -ENOENT;
}

int
ezfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	int ret;

	ret = ezfs_create(dir, dentry, mode | S_IFDIR, true);
	return ret;
}

static int
ezfs_dir_empty(struct buffer_head *bh)
{
	struct ezfs_dir_entry *dentry = (struct ezfs_dir_entry *) bh->b_data;
	int i;

	for (i = 0; i < EZFS_MAX_CHILDREN; i++) {
		if (dentry[i].active)
			return 0;
	}

	return 1;
}

int
ezfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct inode *dentry_inode = d_inode(dentry);
	uint64_t dir_blk_num = get_ezfs_inode(dentry_inode)->dbn;
	struct buffer_head *dir_bh = sb_bread(dir->i_sb, dir_blk_num);
	int result;

	if (!dir_bh)
		return -EIO;

	if (!ezfs_dir_empty(dir_bh)) {
		brelse(dir_bh);
		return -ENOTEMPTY;
	}

	result = ezfs_unlink(dir, dentry);
	brelse(dir_bh);

	if (result)
		return result;

	drop_nlink(dentry_inode);
	drop_nlink(dir);

	return 0;
}

int
ezfs_sync_inode_to_disk(struct buffer_head *i_bh, struct writeback_control *wbc)
{
	mark_buffer_dirty(i_bh);

	if (wbc->sync_mode == WB_SYNC_ALL) {
		sync_dirty_buffer(i_bh);
		if (buffer_req(i_bh) && !buffer_uptodate(i_bh))
			return -EIO;
	}
	return 0;
}

int
ezfs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	struct buffer_head *i_bh = get_ezfs_buffer_head(inode->i_sb);
	struct ezfs_inode *ez_inode = get_ezfs_inode(inode);
	int ret;

	ret = ezfs_update_inode_from_vfs(ez_inode, inode);
	if (ret)
		return ret;

	return ezfs_sync_inode_to_disk(i_bh, wbc);
}

static int
ezfs_init_superblock_buffers(struct super_block *sb,
			     struct ezfs_sb_buffer_heads *sb_buffers)
{
	sb_buffers->sb_bh = sb_bread(sb, EZFS_SUPERBLOCK_DATABLOCK_NUMBER);
	if (!sb_buffers->sb_bh)
		return -EIO;

	sb_buffers->i_store_bh =
	    sb_bread(sb, EZFS_INODE_STORE_DATABLOCK_NUMBER);
	if (!sb_buffers->i_store_bh) {
		brelse(sb_buffers->sb_bh);
		return -EIO;
	}

	return 0;
}

static int
ezfs_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct ezfs_sb_buffer_heads *sb_buffers = sb->s_fs_info;
	struct ezfs_super_block *ez_superblock;
	struct inode *root_inode;

	sb->s_maxbytes = EZFS_BLOCK_SIZE * EZFS_MAX_DATA_BLKS;
	sb->s_magic = EZFS_MAGIC_NUMBER;
	sb->s_op = &ezfs_sb_ops;
	sb->s_time_gran = 1;

	if (!sb_set_blocksize(sb, EZFS_BLOCK_SIZE)
	    || ezfs_init_superblock_buffers(sb, sb_buffers))
		return -EIO;

	ez_superblock = (struct ezfs_super_block *) sb_buffers->sb_bh->b_data;
	ez_superblock->ezfs_lock = kzalloc(sizeof(struct mutex), GFP_KERNEL);
	if (!ez_superblock->ezfs_lock)
		return -ENOMEM;
	mutex_init(ez_superblock->ezfs_lock);

	root_inode = ezfs_iget(sb, EZFS_ROOT_INODE_NUMBER);
	if (IS_ERR(root_inode))
		return PTR_ERR(root_inode);

	sb->s_root = d_make_root(root_inode);
	if (!sb->s_root)
		return -ENOMEM;

	return 0;
}

static void
ezfs_release_buffers(struct ezfs_sb_buffer_heads *buffers)
{
	if (buffers->sb_bh) {
		struct ezfs_super_block *superblock =
		    (struct ezfs_super_block *) buffers->sb_bh->b_data;
		if (superblock->ezfs_lock) {
			mutex_destroy(superblock->ezfs_lock);
			kfree(superblock->ezfs_lock);
		}
		brelse(buffers->sb_bh);
	}
	if (buffers->i_store_bh)
		brelse(buffers->i_store_bh);
}

static void
ezfs_free_fc(struct fs_context *fc)
{
	struct ezfs_sb_buffer_heads *buffers = fc->s_fs_info;

	if (buffers) {
		ezfs_release_buffers(buffers);
		kfree(buffers);
	}
}

static int
ezfs_get_tree(struct fs_context *fc)
{
	return get_tree_bdev(fc, ezfs_fill_super);
}

static int
setup_fs_context(struct fs_context *fc, struct ezfs_sb_buffer_heads *sb_buffers)
{
	static const struct fs_context_operations ezfs_context_ops = {
		.free = ezfs_free_fc,
		.get_tree = ezfs_get_tree,
	};

	fc->s_fs_info = sb_buffers;
	fc->ops = &ezfs_context_ops;
	return 0;
}

int
ezfs_init_fs_context(struct fs_context *fc)
{
	struct ezfs_sb_buffer_heads *sb_buffers =
	    kzalloc(sizeof(*sb_buffers), GFP_KERNEL);
	if (!sb_buffers)
		return -ENOMEM;

	return setup_fs_context(fc, sb_buffers);
}

static void
cleanup_superblock_resources(struct ezfs_sb_buffer_heads *sb_buffers)
{
	if (sb_buffers->sb_bh) {
		struct ezfs_super_block *sb_data =
		    (struct ezfs_super_block *) sb_buffers->sb_bh->b_data;
		if (sb_data->ezfs_lock) {
			mutex_destroy(sb_data->ezfs_lock);
			kfree(sb_data->ezfs_lock);
		}
		brelse(sb_buffers->sb_bh);
	}

	if (sb_buffers->i_store_bh)
		brelse(sb_buffers->i_store_bh);

	kfree(sb_buffers);
}

static void
ezfs_kill_superblock(struct super_block *sb)
{
	cleanup_superblock_resources(sb->s_fs_info);
	kill_block_super(sb);
}

struct file_system_type ezfs_fs_type = {
	.owner = THIS_MODULE,
	.name = "ezfs",
	.init_fs_context = ezfs_init_fs_context,
	.kill_sb = ezfs_kill_superblock,
};

static int __init
init_ezfs_fs(void)
{
	int ret = register_filesystem(&ezfs_fs_type);

	if (likely(ret == 0))
		pr_info("EZFS registered\n");
	else
		pr_err("Failed to register EZFS: %d\n", ret);
	return ret;
}

static void __exit
exit_ezfs_fs(void)
{
	int ret = unregister_filesystem(&ezfs_fs_type);

	if (likely(ret == 0))
		pr_info("EZFS unregistered\n");
	else
		pr_err("Failed to unregister EZFS: %d\n", ret);
}

module_init(init_ezfs_fs);
module_exit(exit_ezfs_fs);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Team 21");
MODULE_DESCRIPTION("EZFS: A Simple File System");
