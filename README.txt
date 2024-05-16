    ----------------------
    Throughout this assignment, my primary focus was to understand and implement the essential components of a file system, specifically our EZFS. The task required direct manipulation of data at a very low level, which enhanced my understanding of how operating systems manage file storage internally.

    Key Learnings:
    1. Superblock Implementation: The superblock is crucial as it contains metadata that the entire filesystem relies on. Implementing it involved understanding how filesystems start and how they keep track of storage with bitmaps for inodes and data blocks. This was foundational for appreciating how data is organized and accessed on a disk.
    2. Inode Lifecycle: By programming the lifecycle of inodes—from their creation and initialization to their use in storing file metadata—I learned about the role of inodes in filesystem architecture. This included how inodes track file attributes such as ownership, permissions, and timestamps, which are essential for system security and integrity.
    3. Directory Entry Management: Implementing directory entries required meticulous attention to detail to correctly manage '.', '..', and regular file entries. This taught me about the hierarchical structure of filesystems and the importance of pointers within directories, which are critical for navigating the filesystem tree efficiently.
    4. Error Handling and Resource Management: Developing robust error handling and resource management routines taught me the importance of reliability and stability in system-level programming. This part of the project underscored the necessity of ensuring that operations either complete successfully or fail without causing system instability or data loss.
    5. Practical Application of Theoretical Concepts: Applying theoretical knowledge about block devices, filesystems, and kernel modules in a practical setting was invaluable. It bridged the gap between conceptual understanding and real-world application, deepening my grasp of filesystem mechanics.

    Challenges:
    - One of the biggest challenges was managing buffer allocations and ensuring that all writes to the disk were accurately reflected in the corresponding data structures. This required a careful design of buffer handling and synchronization mechanisms to avoid data corruption.
    - Another significant challenge was designing the filesystem to handle various edge cases, such as full disk errors and maximum file size limits. These scenarios tested my ability to anticipate and handle exceptional conditions in system software.
    - This was the MOST challenging class including homeworks in the entire CS curriculum.
    ----------------------
