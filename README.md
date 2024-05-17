
# File Storage Management System

### Project Summary

Throughout this project, my primary focus was to understand and implement the essential components of a file system, specifically our file storage. The task required direct manipulation of data at a very low level, which enhanced my understanding of how operating systems manage file storage internally.

### Key Functionalities and Learnings:

1. **Superblock Implementation:**
    - The superblock contains metadata that the entire filesystem relies on. Implementing it involved understanding how filesystems start and keep track of storage with bitmaps for inodes and data blocks. This was foundational for appreciating how data is organized and accessed on a disk.

2. **Inode Lifecycle:**
    - By programming the lifecycle of inodes—from their creation and initialization to their use in storing file metadata—I learned about the role of inodes in filesystem architecture. This included how inodes track file attributes such as ownership, permissions, and timestamps, which are essential for system security and integrity.

3. **Directory Entry Management:**
    - Implementing directory entries required meticulous attention to detail to correctly manage '.', '..', and regular file entries. This taught me about the hierarchical structure of filesystems and the importance of pointers within directories, which are critical for navigating the filesystem tree efficiently.

4. **Error Handling and Resource Management:**
    - Developing robust error handling and resource management routines taught me the importance of reliability and stability in system-level programming. This part of the project underscored the necessity of ensuring that operations either complete successfully or fail without causing system instability or data loss.

5. **Practical Application of Theoretical Concepts:**
    - Applying theoretical knowledge about block devices, filesystems, and kernel modules in a practical setting was invaluable. It bridged the gap between conceptual understanding and real-world application, deepening my grasp of filesystem mechanics.

### Challenges:
- Managing buffer allocations and ensuring that all writes to the disk were accurately reflected in the corresponding data structures. This required a careful design of buffer handling and synchronization mechanisms to avoid data corruption.
- Designing the filesystem to handle various edge cases, such as full disk errors and maximum file size limits. These scenarios tested my ability to anticipate and handle exceptional conditions in system software.
- This was the MOST challenging class including homeworks in the entire CS curriculum.

### Core Functionalities Implemented:

1. **Formatting and Mounting Disks:**
    - Utilized loop devices to create, format, and mount file systems, gaining practical experience with file system operations.

2. **Exploring File Storage:**
    - Created disk images and formatted them as filestorage, exploring its functionalities using reference kernel modules.

3. **Changing the Formatting Program:**
    - Modified the formatting utility to create additional directories and files, enhancing understanding of the on-disk format.

4. **Initializing and Mounting the File System:**
    - Developed the basic functionality to initialize and mount the file system as a kernel module.

5. **Listing Directory Contents:**
    - Implemented the iterate_shared function to list the contents of the root directory, including handling special entries like '.' and '..'.

6. **Accessing Subdirectories:**
    - Enabled navigation into subdirectories and listing their contents, ensuring correct metadata return.

7. **Reading File Contents:**
    - Added support for reading file contents using generic VFS functions, implementing read_iter for efficient data access.

8. **Writing to Existing Files:**
    - Implemented write_iter to handle writing operations, ensuring data integrity and proper synchronization.

9. **Creating New Files:**
    - Enabled the creation of new files with correct metadata initialization, supporting open() with the O_CREAT mode.

10. **Deleting Files:**
    - Developed unlink and evict_inode operations to support file deletion, ensuring proper resource reclamation.

11. **Making and Removing Directories:**
    - Implemented mkdir() and rmdir() functions to create and delete directories, managing directory-specific metadata.

12. **Compiling and Running Executable Files:**
    - Added support for mmap to compile and run executable files, verifying the overall functionality of the file system.

---

This comprehensive approach to developing the file storage provided deep insights into file system mechanics and strengthened my ability to design and implement complex system-level software.
