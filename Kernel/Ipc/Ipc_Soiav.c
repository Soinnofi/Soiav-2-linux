// kernel/ipc/soiav_ipc.c - ПОЛНАЯ ПОДСИСТЕМА IPC
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/spinlock.h>

#define SOIAV_IPC_VERSION "1.0"
#define MAX_IPC_OBJECTS 1024
#define MAX_MESSAGE_SIZE 65536
#define MAX_QUEUE_DEPTH 128

// Типы IPC объектов
enum {
    SOIAV_IPC_UNUSED = 0,
    SOIAV_IPC_QUEUE,     // Очередь сообщений
    SOIAV_IPC_SHM,       // Разделяемая память
    SOIAV_IPC_SEM,       // Семафор
    SOIAV_IPC_PIPE,      // Канал
    SOIAV_IPC_SOCKET,    // Сокет
};

// Сообщение для очереди
struct soiav_ipc_message {
    unsigned long type;
    unsigned long size;
    void *data;
    pid_t sender;
    struct list_head list;
};

// Очередь сообщений
struct soiav_ipc_queue {
    struct list_head messages;
    spinlock_t lock;
    wait_queue_head_t wait;
    unsigned int count;
    unsigned int max_depth;
};

// Разделяемая память
struct soiav_ipc_shm {
    void *address;
    unsigned long size;
    unsigned long flags;
    atomic_t refcount;
    struct mutex lock;
};

// Семафор
struct soiav_ipc_sem {
    int value;
    int max_value;
    spinlock_t lock;
    wait_queue_head_t wait;
};

// Канал (pipe)
struct soiav_ipc_pipe {
    char *buffer;
    unsigned long size;
    unsigned long read_pos;
    unsigned long write_pos;
    int readers;
    int writers;
    spinlock_t lock;
    wait_queue_head_t read_wait;
    wait_queue_head_t write_wait;
};

// Сокет
struct soiav_ipc_socket {
    int domain;
    int type;
    int protocol;
    struct sock *sk;
    void *private;
};

// Основная структура IPC объекта
struct soiav_ipc_object {
    int id;
    int type;
    int flags;
    uid_t uid;
    gid_t gid;
    mode_t mode;
    pid_t creator;
    void *private;
    struct mutex lock;
    atomic_t refcount;
    struct list_head list;
};

// Глобальная таблица IPC объектов
static struct soiav_ipc_object *ipc_objects[MAX_IPC_OBJECTS];
static DEFINE_MUTEX(ipc_table_lock);
static int next_ipc_id = 1;

// ==================== ОЧЕРЕДИ СООБЩЕНИЙ ====================

// Создание очереди сообщений
int soiav_ipc_create_queue(int flags) {
    struct soiav_ipc_object *obj;
    struct soiav_ipc_queue *queue;
    int id;
    
    // Выделяем объект
    obj = kzalloc(sizeof(*obj), GFP_KERNEL);
    if (!obj) return -ENOMEM;
    
    // Выделяем очередь
    queue = kzalloc(sizeof(*queue), GFP_KERNEL);
    if (!queue) {
        kfree(obj);
        return -ENOMEM;
    }
    
    mutex_lock(&ipc_table_lock);
    
    // Ищем свободный ID
    for (id = next_ipc_id; id < MAX_IPC_OBJECTS; id++) {
        if (!ipc_objects[id]) break;
    }
    if (id >= MAX_IPC_OBJECTS) {
        mutex_unlock(&ipc_table_lock);
        kfree(queue);
        kfree(obj);
        return -ENOSPC;
    }
    
    // Инициализация
    obj->id = id;
    obj->type = SOIAV_IPC_QUEUE;
    obj->flags = flags;
    obj->uid = current_uid().val;
    obj->gid = current_gid().val;
    obj->mode = 0600;
    obj->creator = current->pid;
    obj->private = queue;
    mutex_init(&obj->lock);
    atomic_set(&obj->refcount, 1);
    
    INIT_LIST_HEAD(&queue->messages);
    spin_lock_init(&queue->lock);
    init_waitqueue_head(&queue->wait);
    queue->max_depth = MAX_QUEUE_DEPTH;
    
    ipc_objects[id] = obj;
    next_ipc_id = id + 1;
    
    mutex_unlock(&ipc_table_lock);
    
    return id;
}
EXPORT_SYMBOL(soiav_ipc_create_queue);

// Отправка сообщения в очередь
int soiav_ipc_send(int queue_id, unsigned long type, void *data, unsigned long size) {
    struct soiav_ipc_object *obj;
    struct soiav_ipc_queue *queue;
    struct soiav_ipc_message *msg;
    unsigned long flags;
    
    if (queue_id >= MAX_IPC_OBJECTS) return -EINVAL;
    
    mutex_lock(&ipc_table_lock);
    obj = ipc_objects[queue_id];
    if (!obj || obj->type != SOIAV_IPC_QUEUE) {
        mutex_unlock(&ipc_table_lock);
        return -EINVAL;
    }
    atomic_inc(&obj->refcount);
    mutex_unlock(&ipc_table_lock);
    
    if (size > MAX_MESSAGE_SIZE) {
        atomic_dec(&obj->refcount);
        return -EINVAL;
    }
    
    // Выделяем сообщение
    msg = kzalloc(sizeof(*msg), GFP_KERNEL);
    if (!msg) {
        atomic_dec(&obj->refcount);
        return -ENOMEM;
    }
    
    msg->data = kmalloc(size, GFP_KERNEL);
    if (!msg->data) {
        kfree(msg);
        atomic_dec(&obj->refcount);
        return -ENOMEM;
    }
    
    memcpy(msg->data, data, size);
    msg->type = type;
    msg->size = size;
    msg->sender = current->pid;
    
    queue = (struct soiav_ipc_queue*)obj->private;
    
    spin_lock_irqsave(&queue->lock, flags);
    
    if (queue->count >= queue->max_depth) {
        spin_unlock_irqrestore(&queue->lock, flags);
        kfree(msg->data);
        kfree(msg);
        atomic_dec(&obj->refcount);
        return -EAGAIN;
    }
    
    list_add_tail(&msg->list, &queue->messages);
    queue->count++;
    
    spin_unlock_irqrestore(&queue->lock, flags);
    
    wake_up_interruptible(&queue->wait);
    
    atomic_dec(&obj->refcount);
    return 0;
}
EXPORT_SYMBOL(soiav_ipc_send);

// Получение сообщения из очереди
int soiav_ipc_receive(int queue_id, unsigned long *type, void *buffer, unsigned long *size, int flags) {
    struct soiav_ipc_object *obj;
    struct soiav_ipc_queue *queue;
    struct soiav_ipc_message *msg;
    unsigned long lock_flags;
    int ret = 0;
    
    if (queue_id >= MAX_IPC_OBJECTS) return -EINVAL;
    
    mutex_lock(&ipc_table_lock);
    obj = ipc_objects[queue_id];
    if (!obj || obj->type != SOIAV_IPC_QUEUE) {
        mutex_unlock(&ipc_table_lock);
        return -EINVAL;
    }
    atomic_inc(&obj->refcount);
    mutex_unlock(&ipc_table_lock);
    
    queue = (struct soiav_ipc_queue*)obj->private;
    
    spin_lock_irqsave(&queue->lock, lock_flags);
    
    while (list_empty(&queue->messages)) {
        spin_unlock_irqrestore(&queue->lock, lock_flags);
        
        if (flags & IPC_NOWAIT) {
            atomic_dec(&obj->refcount);
            return -EAGAIN;
        }
        
        if (wait_event_interruptible(queue->wait, !list_empty(&queue->messages))) {
            atomic_dec(&obj->refcount);
            return -ERESTARTSYS;
        }
        
        spin_lock_irqsave(&queue->lock, lock_flags);
    }
    
    msg = list_first_entry(&queue->messages, struct soiav_ipc_message, list);
    list_del(&msg->list);
    queue->count--;
    
    spin_unlock_irqrestore(&queue->lock, lock_flags);
    
    if (type) *type = msg->type;
    if (size) *size = msg->size;
    if (buffer && *size >= msg->size) {
        memcpy(buffer, msg->data, msg->size);
    } else {
        ret = -ENOSPC;
    }
    
    kfree(msg->data);
    kfree(msg);
    
    atomic_dec(&obj->refcount);
    return ret;
}
EXPORT_SYMBOL(soiav_ipc_receive);

// ==================== РАЗДЕЛЯЕМАЯ ПАМЯТЬ ====================

// Создание разделяемой памяти
int soiav_ipc_create_shm(unsigned long size, int flags) {
    struct soiav_ipc_object *obj;
    struct soiav_ipc_shm *shm;
    int id;
    
    obj = kzalloc(sizeof(*obj), GFP_KERNEL);
    if (!obj) return -ENOMEM;
    
    shm = kzalloc(sizeof(*shm), GFP_KERNEL);
    if (!shm) {
        kfree(obj);
        return -ENOMEM;
    }
    
    // Выделяем память
    shm->address = kmalloc(size, GFP_KERNEL);
    if (!shm->address) {
        kfree(shm);
        kfree(obj);
        return -ENOMEM;
    }
    
    mutex_lock(&ipc_table_lock);
    
    for (id = next_ipc_id; id < MAX_IPC_OBJECTS; id++) {
        if (!ipc_objects[id]) break;
    }
    if (id >= MAX_IPC_OBJECTS) {
        mutex_unlock(&ipc_table_lock);
        kfree(shm->address);
        kfree(shm);
        kfree(obj);
        return -ENOSPC;
    }
    
    obj->id = id;
    obj->type = SOIAV_IPC_SHM;
    obj->flags = flags;
    obj->uid = current_uid().val;
    obj->gid = current_gid().val;
    obj->mode = 0600;
    obj->creator = current->pid;
    obj->private = shm;
    mutex_init(&obj->lock);
    atomic_set(&obj->refcount, 1);
    
    shm->size = size;
    shm->flags = flags;
    atomic_set(&shm->refcount, 1);
    mutex_init(&shm->lock);
    
    ipc_objects[id] = obj;
    next_ipc_id = id + 1;
    
    mutex_unlock(&ipc_table_lock);
    
    return id;
}
EXPORT_SYMBOL(soiav_ipc_create_shm);

// Подключение к разделяемой памяти
void* soiav_ipc_attach(int shm_id) {
    struct soiav_ipc_object *obj;
    struct soiav_ipc_shm *shm;
    void *addr;
    
    if (shm_id >= MAX_IPC_OBJECTS) return ERR_PTR(-EINVAL);
    
    mutex_lock(&ipc_table_lock);
    obj = ipc_objects[shm_id];
    if (!obj || obj->type != SOIAV_IPC_SHM) {
        mutex_unlock(&ipc_table_lock);
        return ERR_PTR(-EINVAL);
    }
    atomic_inc(&obj->refcount);
    mutex_unlock(&ipc_table_lock);
    
    shm = (struct soiav_ipc_shm*)obj->private;
    
    mutex_lock(&shm->lock);
    atomic_inc(&shm->refcount);
    
    // В реальной ОС здесь нужно отобразить память в адресное пространство процесса
    addr = shm->address;
    
    mutex_unlock(&shm->lock);
    
    return addr;
}
EXPORT_SYMBOL(soiav_ipc_attach);

// Отключение от разделяемой памяти
int soiav_ipc_detach(void *addr) {
    // В реальной ОС здесь нужно убрать отображение
    return 0;
}
EXPORT_SYMBOL(soiav_ipc_detach);

// ==================== СЕМАФОРЫ ====================

// Создание семафора
int soiav_ipc_create_sem(int initial, int max_val) {
    struct soiav_ipc_object *obj;
    struct soiav_ipc_sem *sem;
    int id;
    
    obj = kzalloc(sizeof(*obj), GFP_KERNEL);
    if (!obj) return -ENOMEM;
    
    sem = kzalloc(sizeof(*sem), GFP_KERNEL);
    if (!sem) {
        kfree(obj);
        return -ENOMEM;
    }
    
    mutex_lock(&ipc_table_lock);
    
    for (id = next_ipc_id; id < MAX_IPC_OBJECTS; id++) {
        if (!ipc_objects[id]) break;
    }
    if (id >= MAX_IPC_OBJECTS) {
        mutex_unlock(&ipc_table_lock);
        kfree(sem);
        kfree(obj);
        return -ENOSPC;
    }
    
    obj->id = id;
    obj->type = SOIAV_IPC_SEM;
    obj->private = sem;
    mutex_init(&obj->lock);
    atomic_set(&obj->refcount, 1);
    
    sem->value = initial;
    sem->max_value = max_val;
    spin_lock_init(&sem->lock);
    init_waitqueue_head(&sem->wait);
    
    ipc_objects[id] = obj;
    next_ipc_id = id + 1;
    
    mutex_unlock(&ipc_table_lock);
    
    return id;
}
EXPORT_SYMBOL(soiav_ipc_create_sem);

// Операции над семафором
int soiav_ipc_sem_op(int sem_id, int op, int flags) {
    struct soiav_ipc_object *obj;
    struct soiav_ipc_sem *sem;
    unsigned long irq_flags;
    int ret = 0;
    
    if (sem_id >= MAX_IPC_OBJECTS) return -EINVAL;
    
    mutex_lock(&ipc_table_lock);
    obj = ipc_objects[sem_id];
    if (!obj || obj->type != SOIAV_IPC_SEM) {
        mutex_unlock(&ipc_table_lock);
        return -EINVAL;
    }
    atomic_inc(&obj->refcount);
    mutex_unlock(&ipc_table_lock);
    
    sem = (struct soiav_ipc_sem*)obj->private;
    
    spin_lock_irqsave(&sem->lock, irq_flags);
    
    if (op > 0) {
        // V операция (освобождение)
        sem->value += op;
        if (sem->value > sem->max_value)
            sem->value = sem->max_value;
        spin_unlock_irqrestore(&sem->lock, irq_flags);
        wake_up_interruptible(&sem->wait);
    } else if (op < 0) {
        // P операция (захват)
        while (sem->value + op < 0) {
            spin_unlock_irqrestore(&sem->lock, irq_flags);
            
            if (flags & IPC_NOWAIT) {
                ret = -EAGAIN;
                goto out;
            }
            
            if (wait_event_interruptible(sem->wait, sem->value + op >= 0)) {
                ret = -ERESTARTSYS;
                goto out;
            }
            
            spin_lock_irqsave(&sem->lock, irq_flags);
        }
        
        sem->value += op;
        spin_unlock_irqrestore(&sem->lock, irq_flags);
    } else {
        spin_unlock_irqrestore(&sem->lock, irq_flags);
    }
    
out:
    atomic_dec(&obj->refcount);
    return ret;
}
EXPORT_SYMBOL(soiav_ipc_sem_op);

// ==================== КАНАЛЫ (PIPE) ====================

// Создание канала
int soiav_ipc_create_pipe(unsigned long size) {
    struct soiav_ipc_object *obj;
    struct soiav_ipc_pipe *pipe;
    int id;
    
    obj = kzalloc(sizeof(*obj), GFP_KERNEL);
    if (!obj) return -ENOMEM;
    
    pipe = kzalloc(sizeof(*pipe), GFP_KERNEL);
    if (!pipe) {
        kfree(obj);
        return -ENOMEM;
    }
    
    pipe->buffer = kmalloc(size, GFP_KERNEL);
    if (!pipe->buffer) {
        kfree(pipe);
        kfree(obj);
        return -ENOMEM;
    }
    
    mutex_lock(&ipc_table_lock);
    
    for (id = next_ipc_id; id < MAX_IPC_OBJECTS; id++) {
        if (!ipc_objects[id]) break;
    }
    if (id >= MAX_IPC_OBJECTS) {
        mutex_unlock(&ipc_table_lock);
        kfree(pipe->buffer);
        kfree(pipe);
        kfree(obj);
        return -ENOSPC;
    }
    
    obj->id = id;
    obj->type = SOIAV_IPC_PIPE;
    obj->private = pipe;
    mutex_init(&obj->lock);
    atomic_set(&obj->refcount, 1);
    
    pipe->size = size;
    pipe->read_pos = 0;
    pipe->write_pos = 0;
    pipe->readers = 0;
    pipe->writers = 0;
    spin_lock_init(&pipe->lock);
    init_waitqueue_head(&pipe->read_wait);
    init_waitqueue_head(&pipe->write_wait);
    
    ipc_objects[id] = obj;
    next_ipc_id = id + 1;
    
    mutex_unlock(&ipc_table_lock);
    
    return id;
}
EXPORT_SYMBOL(soiav_ipc_create_pipe);

// Запись в канал
int soiav_ipc_write_pipe(int pipe_id, void *data, unsigned long size, int flags) {
    struct soiav_ipc_object *obj;
    struct soiav_ipc_pipe *pipe;
    unsigned long irq_flags;
    unsigned long written = 0;
    int ret = 0;
    
    if (pipe_id >= MAX_IPC_OBJECTS) return -EINVAL;
    
    mutex_lock(&ipc_table_lock);
    obj = ipc_objects[pipe_id];
    if (!obj || obj->type != SOIAV_IPC_PIPE) {
        mutex_unlock(&ipc_table_lock);
        return -EINVAL;
    }
    atomic_inc(&obj->refcount);
    mutex_unlock(&ipc_table_lock);
    
    pipe = (struct soiav_ipc_pipe*)obj->private;
    
    while (written < size) {
        spin_lock_irqsave(&pipe->lock, irq_flags);
        
        // Проверяем свободное место
        unsigned long free_space;
        if (pipe->write_pos >= pipe->read_pos) {
            free_space = pipe->size - pipe->write_pos + pipe->read_pos;
        } else {
            free_space = pipe->read_pos - pipe->write_pos;
        }
        
        if (free_space == 0) {
            spin_unlock_irqrestore(&pipe->lock, irq_flags);
            
            if (flags & IPC_NOWAIT) {
                ret = written ? written : -EAGAIN;
                goto out;
            }
            
            if (wait_event_interruptible(pipe->write_wait, free_space > 0)) {
                ret = written ? written : -ERESTARTSYS;
                goto out;
            }
            
            continue;
        }
        
        // Сколько можем записать
        unsigned long to_write = size - written;
        if (to_write > free_space)
            to_write = free_space;
        
        // Запись с учетом кольцевого буфера
        if (pipe->write_pos + to_write <= pipe->size) {
            memcpy(pipe->buffer + pipe->write_pos, data + written, to_write);
            pipe->write_pos += to_write;
            if (pipe->write_pos == pipe->size)
                pipe->write_pos = 0;
        } else {
            unsigned long first_part = pipe->size - pipe->write_pos;
            memcpy(pipe->buffer + pipe->write_pos, data + written, first_part);
            memcpy(pipe->buffer, data + written + first_part, to_write - first_part);
            pipe->write_pos = to_write - first_part;
        }
        
        written += to_write;
        
        spin_unlock_irqrestore(&pipe->lock, irq_flags);
        
        wake_up_interruptible(&pipe->read_wait);
    }
    
out:
    atomic_dec(&obj->refcount);
    return written;
}
EXPORT_SYMBOL(soiav_ipc_write_pipe);

// Чтение из канала
int soiav_ipc_read_pipe(int pipe_id, void *buffer, unsigned long size, int flags) {
    struct soiav_ipc_object *obj;
    struct soiav_ipc_pipe *pipe;
    unsigned long irq_flags;
    unsigned long read = 0;
    int ret = 0;
    
    if (pipe_id >= MAX_IPC_OBJECTS) return -EINVAL;
    
    mutex_lock(&ipc_table_lock);
    obj = ipc_objects[pipe_id];
    if (!obj || obj->type != SOIAV_IPC_PIPE) {
        mutex_unlock(&ipc_table_lock);
        return -EINVAL;
    }
    atomic_inc(&obj->refcount);
    mutex_unlock(&ipc_table_lock);
    
    pipe = (struct soiav_ipc_pipe*)obj->private;
    
    while (read < size) {
        spin_lock_irqsave(&pipe->lock, irq_flags);
        
        // Проверяем наличие данных
        unsigned long available;
        if (pipe->write_pos >= pipe->read_pos) {
            available = pipe->write_pos - pipe->read_pos;
        } else {
            available = pipe->size - pipe->read_pos + pipe->write_pos;
        }
        
        if (available == 0) {
            spin_unlock_irqrestore(&pipe->lock, irq_flags);
            
            if (flags & IPC_NOWAIT) {
                ret = read ? read : -EAGAIN;
                goto out;
            }
            
            if (wait_event_interruptible(pipe->read_wait, available > 0)) {
                ret = read ? read : -ERESTARTSYS;
                goto out;
            }
            
            continue;
        }
        
        // Сколько можем прочитать
        unsigned long to_read = size - read;
        if (to_read > available)
            to_read = available;
        
        // Чтение с учетом кольцевого буфера
        if (pipe->read_pos + to_read <= pipe->size) {
            memcpy(buffer + read, pipe->buffer + pipe->read_pos, to_read);
            pipe->read_pos += to_read;
            if (pipe->read_pos == pipe->size)
                pipe->read_pos = 0;
        } else {
            unsigned long first_part = pipe->size - pipe->read_pos;
            memcpy(buffer + read, pipe->buffer + pipe->read_pos, first_part);
            memcpy(buffer + read + first_part, pipe->buffer, to_read - first_part);
            pipe->read_pos = to_read - first_part;
        }
        
        read += to_read;
        
        spin_unlock_irqrestore(&pipe->lock, irq_flags);
        
        wake_up_interruptible(&pipe->write_wait);
    }
    
out:
    atomic_dec(&obj->refcount);
    return read;
}
EXPORT_SYMBOL(soiav_ipc_read_pipe);

// ==================== УПРАВЛЕНИЕ IPC ====================

// Закрытие IPC объекта
int soiav_ipc_close(int id) {
    struct soiav_ipc_object *obj;
    
    if (id >= MAX_IPC_OBJECTS) return -EINVAL;
    
    mutex_lock(&ipc_table_lock);
    obj = ipc_objects[id];
    if (!obj) {
        mutex_unlock(&ipc_table_lock);
        return -EINVAL;
    }
    
    if (atomic_dec_and_test(&obj->refcount)) {
        // Освобождаем ресурсы в зависимости от типа
        switch(obj->type) {
            case SOIAV_IPC_QUEUE: {
                struct soiav_ipc_queue *q = obj->private;
                struct soiav_ipc_message *msg, *tmp;
                
                list_for_each_entry_safe(msg, tmp, &q->messages, list) {
                    kfree(msg->data);
                    kfree(msg);
                }
                kfree(q);
                break;
            }
            case SOIAV_IPC_SHM: {
                struct soiav_ipc_shm *s = obj->private;
                kfree(s->address);
                kfree(s);
                break;
            }
            case SOIAV_IPC_SEM: {
                struct soiav_ipc_sem *s = obj->private;
                kfree(s);
                break;
            }
            case SOIAV_IPC_PIPE: {
                struct soiav_ipc_pipe *p = obj->private;
                kfree(p->buffer);
                kfree(p);
                break;
            }
            case SOIAV_IPC_SOCKET: {
                struct soiav_ipc_socket *s = obj->private;
                if (s->sk)
                    sock_release(s->sk);
                kfree(s);
                break;
            }
        }
        
        kfree(obj);
        ipc_objects[id] = NULL;
        
        if (id < next_ipc_id)
            next_ipc_id = id;
    }
    
    mutex_unlock(&ipc_table_lock);
    return 0;
}
EXPORT_SYMBOL(soiav_ipc_close);

// Получение статистики IPC
int soiav_ipc_stats(int id, void *stats) {
    struct soiav_ipc_object *obj;
    
    if (id >= MAX_IPC_OBJECTS) return -EINVAL;
    
    mutex_lock(&ipc_table_lock);
    obj = ipc_objects[id];
    if (!obj) {
        mutex_unlock(&ipc_table_lock);
        return -EINVAL;
    }
    
    // Заполняем статистику в зависимости от типа
    // ...
    
    mutex_unlock(&ipc_table_lock);
    return 0;
}
EXPORT_SYMBOL(soiav_ipc_stats);

// Инициализация IPC подсистемы
static int __init soiav_ipc_init(void) {
    printk(KERN_INFO "Soiav IPC: Загрузка подсистемы v%s\n", 
           SOIAV_IPC_VERSION);
    
    memset(ipc_objects, 0, sizeof(ipc_objects));
    mutex_init(&ipc_table_lock);
    
    return 0;
}

// Выгрузка IPC подсистемы
static void __exit soiav_ipc_exit(void) {
    int i;
    
    for (i = 0; i < MAX_IPC_OBJECTS; i++) {
        if (ipc_objects[i])
            soiav_ipc_close(i);
    }
    
    printk(KERN_INFO "Soiav IPC: Выгружена\n");
}

module_init(soiav_ipc_init);
module_exit(soiav_ipc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Soiav Systems");
MODULE_DESCRIPTION("Soiav OS 2 IPC Subsystem");
