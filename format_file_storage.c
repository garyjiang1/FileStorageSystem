#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

/* These are the same on a 64-bit architecture */
#define timespec64 timespec

#include "ezfs.h"

void
passert(int condition, char *message)
{
	printf("[%s] %s\n", condition ? "OK" : "FAIL", message);
	if (!condition) {
		fprintf(stderr, "Fatal error: %s\n", message);
		exit(1);
	}
}

void
inode_reset(struct ezfs_inode *inode)
{
	struct timespec current_time;

	memset(inode, 0, sizeof(*inode));
	memset(&current_time, 0, sizeof(current_time));
	inode->uid = 1000;
	inode->gid = 1000;
	clock_gettime(CLOCK_REALTIME, &current_time);
	inode->i_atime = inode->i_mtime = inode->i_ctime = current_time;
}

void
dentry_reset(struct ezfs_dir_entry *dentry)
{
	memset(dentry, 0, sizeof(*dentry));
}

int
open_file(const char *filename, int flags)
{
	int fd = open(filename, flags);

	if (fd == -1) {
		perror("Error opening file");
		exit(1);	// Exiting here for now, could handle differently
	}
	return fd;
}

ssize_t
read_file(int fd, void *buf, size_t count, const char *errmsg)
{
	ssize_t ret = read(fd, buf, count);

	if (ret == -1) {
		perror(errmsg);
		close(fd);
		exit(1);
	}
	return ret;
}

int
main(int argc, char *argv[])
{
	if (argc != 2) {
		printf("Usage: ./format_disk_as_ezfs DEVICE_NAME.\n");
		return -1;
	}

	int fd = open_file(argv[1], O_RDWR);

	struct ezfs_super_block sb;
	struct ezfs_inode inode;
	struct ezfs_dir_entry dentry;
	char *hello_contents = "Hello world!\n";
	char *names_contents = "Jiawei; Monirul; Faiza\n";
	char buf[EZFS_BLOCK_SIZE], pbuf[EZFS_BLOCK_SIZE * 8],
	    bbuf[EZFS_BLOCK_SIZE * 2];
	const char zeroes[EZFS_BLOCK_SIZE] = { 0 };
	ssize_t len;

	memset(&sb, 0, sizeof(sb));

	int fp = open_file("./big_files/big_img.jpeg", O_RDWR);
	ssize_t pret =
	    read_file(fp, pbuf, EZFS_BLOCK_SIZE * 9, "Read big img contents");
	close(fp);

	fp = open_file("./big_files/big_txt.txt", O_RDWR);
	ssize_t bret =
	    read_file(fp, bbuf, EZFS_BLOCK_SIZE * 2, "Read big txt contents");
	close(fp);

	sb.version = 1;
	sb.magic = EZFS_MAGIC_NUMBER;
	for (int i = 0; i < 6; ++i)
		SETBIT(sb.free_inodes, i);
	for (int i = 0; i < 14; ++i)
		SETBIT(sb.free_data_blocks, i);

	ssize_t ret = write(fd, &sb, sizeof(sb));

	passert(ret == EZFS_BLOCK_SIZE, "Write superblock");

	inode_reset(&inode);
	inode.mode = S_IFDIR | 0777;
	inode.nlink = 3;	// add 1 to 2 because adding another directory
	inode.dbn = EZFS_ROOT_DATABLOCK_NUMBER;
	inode.file_size = EZFS_BLOCK_SIZE;
	inode.nblocks = 1;

	ret = write(fd, (char *) &inode, sizeof(inode));
	passert(ret == sizeof(inode), "Write root inode");

	inode_reset(&inode);
	inode.nlink = 1;
	inode.mode = S_IFREG | 0666;
	inode.dbn = EZFS_ROOT_DATABLOCK_NUMBER + 1;
	inode.file_size = strlen(hello_contents);
	inode.nblocks = 1;

	ret = write(fd, (char *) &inode, sizeof(inode));
	passert(ret == sizeof(inode), "Write hello.txt inode");

	inode_reset(&inode);
	inode.mode = S_IFDIR | 0777;
	inode.nlink = 2;
	inode.dbn = EZFS_ROOT_DATABLOCK_NUMBER + 2;
	inode.file_size = EZFS_BLOCK_SIZE;
	inode.nblocks = 1;

	ret = write(fd, (char *) &inode, sizeof(inode));
	passert(ret == sizeof(inode), "Write subdir inode");

	inode_reset(&inode);
	inode.nlink = 1;
	inode.mode = S_IFREG | 0666;
	inode.dbn = EZFS_ROOT_DATABLOCK_NUMBER + 3;
	inode.file_size = strlen(names_contents);
	inode.nblocks = 1;

	ret = write(fd, (char *) &inode, sizeof(inode));
	passert(ret == sizeof(inode), "Write names.txt inode");

	inode_reset(&inode);
	inode.nlink = 1;
	inode.mode = S_IFREG | 0666;
	inode.dbn = EZFS_ROOT_DATABLOCK_NUMBER + 4;
	inode.file_size = pret;
	inode.nblocks = 8;

	ret = write(fd, (char *) &inode, sizeof(inode));
	passert(ret == sizeof(inode), "Write big_img.jpeg inode");

	inode_reset(&inode);
	inode.nlink = 1;
	inode.mode = S_IFREG | 0666;
	inode.dbn = EZFS_ROOT_DATABLOCK_NUMBER + 4 + 8;
	inode.file_size = bret;
	inode.nblocks = 2;

	ret = write(fd, (char *) &inode, sizeof(inode));
	passert(ret == sizeof(inode), "Write big_txt.txt inode");

	ret =
	    lseek(fd, EZFS_BLOCK_SIZE - 6 * sizeof(struct ezfs_inode),
		  SEEK_CUR);
	passert(ret >= 0, "Seek past inode table");

	dentry_reset(&dentry);
	strncpy(dentry.filename, "hello.txt", sizeof(dentry.filename));
	dentry.active = 1;
	dentry.inode_no = EZFS_ROOT_INODE_NUMBER + 1;

	ret = write(fd, (char *) &dentry, sizeof(dentry));
	passert(ret == sizeof(dentry), "Write dentry for hello.txt");

	dentry_reset(&dentry);
	strncpy(dentry.filename, "subdir", sizeof(dentry.filename));
	dentry.active = 1;
	dentry.inode_no = EZFS_ROOT_INODE_NUMBER + 2;

	ret = write(fd, (char *) &dentry, sizeof(dentry));
	passert(ret == sizeof(dentry), "Write dentry for subdir");

	len = EZFS_BLOCK_SIZE - 2 * sizeof(struct ezfs_dir_entry);
	ret = write(fd, zeroes, len);
	passert(ret == len, "Pad to end of root dentries");

	len = strlen(hello_contents);
	strncpy(buf, hello_contents, len);
	ret = write(fd, buf, len);
	passert(ret == len, "Write hello.txt contents");

	ret = lseek(fd, EZFS_BLOCK_SIZE - len, SEEK_CUR);
	passert(ret >= 0, "Seek to next file block");

	dentry_reset(&dentry);
	strncpy(dentry.filename, "names.txt", sizeof(dentry.filename));
	dentry.active = 1;
	dentry.inode_no = EZFS_ROOT_INODE_NUMBER + 3;

	ret = write(fd, (char *) &dentry, sizeof(dentry));
	passert(ret == sizeof(dentry), "Write dentry for names.txt");

	dentry_reset(&dentry);
	strncpy(dentry.filename, "big_img.jpeg", sizeof(dentry.filename));
	dentry.active = 1;
	dentry.inode_no = EZFS_ROOT_INODE_NUMBER + 4;

	ret = write(fd, (char *) &dentry, sizeof(dentry));
	passert(ret == sizeof(dentry), "Write dentry for big_img.jpeg");

	dentry_reset(&dentry);
	strncpy(dentry.filename, "big_txt.txt", sizeof(dentry.filename));
	dentry.active = 1;
	dentry.inode_no = EZFS_ROOT_INODE_NUMBER + 5;

	ret = write(fd, (char *) &dentry, sizeof(dentry));
	passert(ret == sizeof(dentry), "Write dentry for big_txt.txt");

	len = EZFS_BLOCK_SIZE - 3 * sizeof(struct ezfs_dir_entry);
	ret = write(fd, zeroes, len);
	passert(ret == len, "Pad to end of subdir dentries");

	len = strlen(names_contents);
	strncpy(buf, names_contents, len);
	ret = write(fd, buf, len);
	passert(ret == len, "Write names.txt contents");

	ret = lseek(fd, EZFS_BLOCK_SIZE - len, SEEK_CUR);
	passert(ret >= 0, "Seek to next file block for big_img.jpeg");

	ret = write(fd, pbuf, pret);
	passert(ret == pret, "Write big_img.jpeg contents");

	ret = lseek(fd, EZFS_BLOCK_SIZE * 8 - pret, SEEK_CUR);
	passert(ret >= 0, "Seek to next file block for big_txt.txt");

	ret = write(fd, bbuf, bret);
	passert(ret == bret, "Write big_txt.txt contents");

	ret = fsync(fd);
	passert(ret == 0, "Flush writes to disk");

	close(fd);
	printf("Device [%s] formatted successfully.\n", argv[1]);

	return 0;
}
