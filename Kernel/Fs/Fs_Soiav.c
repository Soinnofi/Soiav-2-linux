// kernel/fs/soiav_fs.c - SoiavFS собственная файловая система
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/time.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/buffer_head.h>
#include <linux/magic.h>

#define SOIAVFS_MAGIC 0x534F4941  // "SOIA"
#define SOIAVFS_VERSION 1
#define SOIAVFS_BLOCK_SIZE 4096
#define SOIAVFS_MAX_NAME 255
#define SOIAVFS_MAX_FILES 65536

// Суперблок
struct soiavfs_superblock {
    __le32 magic;           // Магия
    __le32 version;         // Версия ФС
    __le64 block_count;     // Всего блоков
    __le64 free_blocks;     // Свободных блоков
    __le64 inode_count;     // Всего инодов
    __le64 free_inodes;     // Свободных инодов
    __le64 first_inode;     // Первый инод
    __le64 first_data;      // Первый блок данных
    char volume_name[64];    // Имя тома
    __le64 mount_time;       // Время монтирования
    __le64 write_time;       // Время записи
    __le32 flags;            // Флаги
    __le32 reserved[128];    // Зарезервировано
};

// Инод
struct soiavfs_inode {
    __le16 mode;            // Режим доступа
    __le16 uid;             // Владелец
    __le16 gid;             // Группа
    __le64 size;            // Размер
    __le64 atime;           // Время доступа
    __le64 mtime;           // Время модификации
    __le64 ctime;           // Время создания
    __le16 links;           // Ссылки
    __le32 blocks;          // Блоков занято
    __le32 flags;           // Флаги
    __le64 direct[12];      // Прямые блоки
    __le64 indirect;        // Непрямой блок
    __le64 double_indirect; // Двойной непрямой
    __le64 reserved[16];    // Зарезервировано
};

// Запись директории
struct soiavfs_dirent {
    __le64 inode;           // Инод
    __le16 rec_len;         // Длина записи
    __u8 name_len;          // Длина имени
    __u8 file_type;         // Тип файла
    char name[SOIAVFS_MAX_NAME]; // Имя
};

// Контекст ФС
struct soiavfs_info {
    struct soiavfs_superblock *sb;
    struct buffer_head *bh;
    struct super_block *vfs_sb;
};

// Создание файловой системы
static int soiavfs_format(struct block_device *bdev) {
    struct buffer_head *bh;
    struct soiavfs_superblock *sb;
    int blocks = bdev->bd_inode->i_size / SOIAVFS_BLOCK_SIZE;
    
    // Выделяем буфер для суперблока
    bh = __getblk(bdev, 0, SOIAVFS_BLOCK_SIZE);
    if (!bh) return -ENOMEM;
    
    sb = (struct soiavfs_superblock*)bh->b_data;
    memset(sb, 0, SOIAVFS_BLOCK_SIZE);
    
    // Заполняем суперблок
    sb->magic = cpu_to_le32(SOIAVFS_MAGIC);
    sb->version = cpu_to_le32(SOIAVFS_VERSION);
    sb->block_count = cpu_to_le64(blocks);
    sb->free_blocks = cpu_to_le64(blocks - 10); // 10 блоков служебные
    sb->inode_count = cpu_to_le64(SOIAVFS_MAX_FILES);
    sb->free_inodes = cpu_to_le64(SOIAVFS_MAX_FILES - 2); // root и lost+found
    sb->first_inode = cpu_to_le64(2); // Начинаем со 2-го
    sb->first_data = cpu_to_le64(100); // Данные с 100-го блока
    strcpy(sb->volume_name, "Soiav Volume");
    sb->mount_time = cpu_to_le64(kernel_get_time());
    sb->write_time = cpu_to_le64(kernel_get_time());
    
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);
    
    // Создаем корневой каталог
    bh = __getblk(bdev, 2, SOIAVFS_BLOCK_SIZE);
    struct soiavfs_inode *root_inode = (struct soiavfs_inode*)bh->b_data;
    memset(root_inode, 0, SOIAVFS_BLOCK_SIZE);
    
    root_inode->mode = cpu_to_le16(S_IFDIR | 0755);
    root_inode->uid = cpu_to_le16(0);
    root_inode->gid = cpu_to_le16(0);
    root_inode->links = cpu_to_le16(2); // . и ..
    root_inode->blocks = cpu_to_le32(1);
    root_inode->direct[0] = cpu_to_le64(100); // Блок данных
    
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);
    
    // Создаем блок данных корня
    bh = __getblk(bdev, 100, SOIAVFS_BLOCK_SIZE);
    struct soiavfs_dirent *de = (struct soiavfs_dirent*)bh->b_data;
    
    // Запись "."
    de->inode = cpu_to_le64(2);
    de->rec_len = cpu_to_le16(sizeof(struct soiavfs_dirent));
    de->name_len = 1;
    de->file_type = DT_DIR;
    strcpy(de->name, ".");
    de = (struct soiavfs_dirent*)((char*)de + de->rec_len);
    
    // Запись ".."
    de->inode = cpu_to_le64(2);
    de->rec_len = cpu_to_le16(sizeof(struct soiavfs_dirent));
    de->name_len = 2;
    de->file_type = DT_DIR;
    strcpy(de->name, "..");
    
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);
    
    return 0;
}

// Чтение суперблока
static int soiavfs_fill_super(struct super_block *sb, void *data, int silent) {
    struct buffer_head *bh;
    struct soiavfs_superblock *dsb;
    struct inode *root;
    
    // Читаем суперблок
    bh = sb_bread(sb, 0);
    if (!bh) {
        printk(KERN_ERR "SoiavFS: Не удалось прочитать суперблок\n");
        return -EIO;
    }
    
    dsb = (struct soiavfs_superblock*)bh->b_data;
    
    // Проверяем магию
    if (le32_to_cpu(dsb->magic) != SOIAVFS_MAGIC) {
        brelse(bh);
        printk(KERN_ERR "SoiavFS: Неверная магия ФС\n");
        return -EINVAL;
    }
    
    // Сохраняем информацию
    sb->s_fs_info = dsb;
    sb->s_magic = SOIAVFS_MAGIC;
    sb->s_op = &soiavfs_sops;
    sb->s_maxbytes = MAX_LFS_FILESIZE;
    sb->s_blocksize = SOIAVFS_BLOCK_SIZE;
    sb->s_blocksize_bits = 12; // 2^12 = 4096
    
    // Создаем корневой инод
    root = soiavfs_iget(sb, 2);
    if (IS_ERR(root)) {
        brelse(bh);
        return PTR_ERR(root);
    }
    
    sb->s_root = d_make_root(root);
    if (!sb->s_root) {
        iput(root);
        brelse(bh);
        return -ENOMEM;
    }
    
    brelse(bh);
    return 0;
}

// Получение инода
struct inode *soiavfs_iget(struct super_block *sb, unsigned long ino) {
    struct inode *inode;
    struct buffer_head *bh;
    struct soiavfs_inode *di;
    
    inode = iget_locked(sb, ino);
    if (!inode) return ERR_PTR(-ENOMEM);
    
    if (!(inode->i_state & I_NEW)) return inode;
    
    // Читаем инод с диска
    bh = sb_bread(sb, ino);
    if (!bh) {
        iget_failed(inode);
        return ERR_PTR(-EIO);
    }
    
    di = (struct soiavfs_inode*)bh->b_data;
    
    // Заполняем VFS inode
    inode->i_mode = le16_to_cpu(di->mode);
    inode->i_uid = make_kuid(&init_user_ns, le16_to_cpu(di->uid));
    inode->i_gid = make_kgid(&init_user_ns, le16_to_cpu(di->gid));
    inode->i_size = le64_to_cpu(di->size);
    inode->i_atime.tv_sec = le64_to_cpu(di->atime);
    inode->i_mtime.tv_sec = le64_to_cpu(di->mtime);
    inode->i_ctime.tv_sec = le64_to_cpu(di->ctime);
    inode->i_blocks = le32_to_cpu(di->blocks);
    
    // Операции в зависимости от типа
    if (S_ISREG(inode->i_mode)) {
        inode->i_op = &soiavfs_file_inode_ops;
        inode->i_fop = &soiavfs_file_ops;
    } else if (S_ISDIR(inode->i_mode)) {
        inode->i_op = &soiavfs_dir_inode_ops;
        inode->i_fop = &soiavfs_dir_ops;
    }
    
    brelse(bh);
    unlock_new_inode(inode);
    
    return inode;
}

// Чтение файла
static int soiavfs_readpage(struct file *file, struct page *page) {
    struct inode *inode = page->mapping->host;
    struct buffer_head *bh;
    char *data;
    loff_t offset = page_offset(page);
    
    bh = sb_bread(inode->i_sb, offset / SOIAVFS_BLOCK_SIZE);
    if (!bh) return -EIO;
    
    data = kmap(page);
    memcpy(data, bh->b_data, SOIAVFS_BLOCK_SIZE);
    kunmap(page);
    
    brelse(bh);
    SetPageUptodate(page);
    unlock_page(page);
    
    return 0;
}

// Запись файла
static int soiavfs_writepage(struct page *page, struct writeback_control *wbc) {
    struct inode *inode = page->mapping->host;
    struct buffer_head *bh;
    char *data;
    loff_t offset = page_offset(page);
    
    bh = sb_bread(inode->i_sb, offset / SOIAVFS_BLOCK_SIZE);
    if (!bh) return -EIO;
    
    data = kmap(page);
    memcpy(bh->b_data, data, SOIAVFS_BLOCK_SIZE);
    kunmap(page);
    
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);
    
    SetPageUptodate(page);
    unlock_page(page);
    
    return 0;
}

// Операции с файлами
struct address_space_operations soiavfs_aops = {
    .readpage = soiavfs_readpage,
    .writepage = soiavfs_writepage
};

// Чтение директории
static int soiavfs_readdir(struct file *file, struct dir_context *ctx) {
    struct inode *inode = file_inode(file);
    struct buffer_head *bh;
    struct soiavfs_dirent *de;
    loff_t pos = ctx->pos;
    
    if (pos >= inode->i_size) return 0;
    
    bh = sb_bread(inode->i_sb, pos / SOIAVFS_BLOCK_SIZE);
    if (!bh) return -EIO;
    
    de = (struct soiavfs_dirent*)(bh->b_data + (pos % SOIAVFS_BLOCK_SIZE));
    
    while (pos < inode->i_size && de->inode) {
        if (!dir_emit(ctx, de->name, de->name_len,
                     le64_to_cpu(de->inode), de->file_type)) {
            brelse(bh);
            return 0;
        }
        
        pos += le16_to_cpu(de->rec_len);
        ctx->pos = pos;
        
        if (pos >= inode->i_size) break;
        
        de = (struct soiavfs_dirent*)(bh->b_data + 
              ((loff_t)de->rec_len % SOIAVFS_BLOCK_SIZE));
    }
    
    brelse(bh);
    return 0;
}

// Создание файла
static int soiavfs_create(struct inode *dir, struct dentry *dentry,
                          umode_t mode, bool excl) {
    struct inode *inode;
    struct buffer_head *bh;
    struct soiavfs_inode *di;
    struct soiavfs_dirent *de;
    int ino = soiavfs_find_free_inode(dir->i_sb);
    
    if (ino < 0) return -ENOSPC;
    
    // Создаем инод
    inode = new_inode(dir->i_sb);
    if (!inode) return -ENOMEM;
    
    inode->i_ino = ino;
    inode->i_mode = mode;
    inode->i_uid = current_fsuid();
    inode->i_gid = current_fsgid();
    inode->i_size = 0;
    inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
    inode->i_blocks = 0;
    
    // Записываем инод на диск
    bh = sb_bread(dir->i_sb, ino);
    if (!bh) {
        iput(inode);
        return -EIO;
    }
    
    di = (struct soiavfs_inode*)bh->b_data;
    di->mode = cpu_to_le16(mode);
    di->uid = cpu_to_le16(from_kuid(&init_user_ns, inode->i_uid));
    di->gid = cpu_to_le16(from_kgid(&init_user_ns, inode->i_gid));
    di->size = 0;
    di->atime = cpu_to_le64(inode->i_atime.tv_sec);
    di->mtime = cpu_to_le64(inode->i_mtime.tv_sec);
    di->ctime = cpu_to_le64(inode->i_ctime.tv_sec);
    di->blocks = 0;
    
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);
    
    // Добавляем запись в директорию
    bh = sb_bread(dir->i_sb, 100); // TODO: найти свободный блок
    if (!bh) {
        iput(inode);
        return -EIO;
    }
    
    de = (struct soiavfs_dirent*)bh->b_data;
    while (de->inode) {
        de = (struct soiavfs_dirent*)((char*)de + le16_to_cpu(de->rec_len));
    }
    
    de->inode = cpu_to_le64(ino);
    de->rec_len = cpu_to_le16(sizeof(struct soiavfs_dirent));
    de->name_len = strlen(dentry->d_name.name);
    de->file_type = DT_REG;
    strcpy(de->name, dentry->d_name.name);
    
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);
    
    d_instantiate(dentry, inode);
    return 0;
}

// Операции с инодами
struct inode_operations soiavfs_file_inode_ops = {
    .getattr = simple_getattr,
};

struct inode_operations soiavfs_dir_inode_ops = {
    .create = soiavfs_create,
    .lookup = simple_lookup,
};

struct file_operations soiavfs_file_ops = {
    .read_iter = generic_file_read_iter,
    .write_iter = generic_file_write_iter,
    .mmap = generic_file_mmap,
    .open = generic_file_open,
};

struct file_operations soiavfs_dir_ops = {
    .iterate = soiavfs_readdir,
    .fsync = generic_file_fsync,
};

// Операции суперблока
static const struct super_operations soiavfs_sops = {
    .alloc_inode = soiavfs_alloc_inode,
    .destroy_inode = soiavfs_destroy_inode,
    .write_inode = soiavfs_write_inode,
    .drop_inode = generic_delete_inode,
    .statfs = simple_statfs,
    .put_super = soiavfs_put_super,
};

// Монтирование
static struct dentry *soiavfs_mount(struct file_system_type *fs_type,
                                     int flags, const char *dev_name,
                                     void *data) {
    return mount_bdev(fs_type, flags, dev_name, data, soiavfs_fill_super);
}

static struct file_system_type soiavfs_fs_type = {
    .owner = THIS_MODULE,
    .name = "soiavfs",
    .mount = soiavfs_mount,
    .kill_sb = kill_block_super,
    .fs_flags = FS_REQUIRES_DEV,
};

// Инициализация ФС
static int __init soiavfs_init(void) {
    printk(KERN_INFO "SoiavFS: Загрузка файловой системы v%d\n", 
           SOIAVFS_VERSION);
    return register_filesystem(&soiavfs_fs_type);
}

// Выгрузка ФС
static void __exit soiavfs_exit(void) {
    unregister_filesystem(&soiavfs_fs_type);
    printk(KERN_INFO "SoiavFS: Выгружена\n");
}

module_init(soiavfs_init);
module_exit(soiavfs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Soiav Systems");
MODULE_DESCRIPTION("Soiav OS 2 Native Filesystem");
