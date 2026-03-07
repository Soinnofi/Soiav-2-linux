// userland/libc/soiav_libc.h - ЗАГОЛОВОЧНЫЙ ФАЙЛ
#ifndef _SOIAV_LIBC_H
#define _SOIAV_LIBC_H

#include <stddef.h>
#include <stdarg.h>

// Стандартные типы
typedef unsigned int size_t;
typedef int ssize_t;
typedef long off_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef unsigned int pid_t;
typedef unsigned int mode_t;

// Стандартные константы
#define NULL ((void*)0)
#define EOF (-1)
#define BUFSIZ 8192
#define FILENAME_MAX 256
#define FOPEN_MAX 16

// Флаги открытия файлов
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0100
#define O_EXCL 0200
#define O_TRUNC 01000
#define O_APPEND 02000

// Поиск в файле
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

// Ошибки
#define EPERM 1
#define ENOENT 2
#define ESRCH 3
#define EINTR 4
#define EIO 5
#define ENXIO 6
#define E2BIG 7
#define ENOEXEC 8
#define EBADF 9
#define ECHILD 10
#define EAGAIN 11
#define ENOMEM 12
#define EACCES 13
#define EFAULT 14
#define ENOTBLK 15
#define EBUSY 16
#define EEXIST 17
#define EXDEV 18
#define ENODEV 19
#define ENOTDIR 20
#define EISDIR 21
#define EINVAL 22
#define ENFILE 23
#define EMFILE 24
#define ENOTTY 25
#define ETXTBSY 26
#define EFBIG 27
#define ENOSPC 28
#define ESPIPE 29
#define EROFS 30
#define EMLINK 31
#define EPIPE 32
#define EDOM 33
#define ERANGE 34

// Структура FILE
typedef struct _FILE {
    int fd;
    unsigned char *buf;
    size_t buf_size;
    size_t buf_pos;
    size_t buf_len;
    int flags;
    int eof;
    int error;
} FILE;

// Внешние переменные
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

// ==================== СИСТЕМНЫЕ ВЫЗОВЫ ====================

// Системные вызовы Soiav
long soiav_syscall(long number, ...);

// Системные вызовы для IPC
int soiav_ipc_create_queue(int flags);
int soiav_ipc_send(int queue_id, unsigned long type, void *data, unsigned long size);
int soiav_ipc_receive(int queue_id, unsigned long *type, void *buffer, unsigned long *size, int flags);
int soiav_ipc_create_shm(unsigned long size, int flags);
void* soiav_ipc_attach(int shm_id);
int soiav_ipc_detach(void *addr);
int soiav_ipc_create_sem(int initial, int max_val);
int soiav_ipc_sem_op(int sem_id, int op, int flags);
int soiav_ipc_close(int id);

// Системные вызовы для GUI
int soiav_create_window(int width, int height, int flags, const char *title);
int soiav_draw_rect(int window, int x, int y, int w, int h, unsigned int color);
int soiav_draw_text(int window, int x, int y, const char *text, unsigned int color);
int soiav_get_input(int window, void *event);

// Системные вызовы для файлов
int soiav_open(const char *path, int flags, int mode);
int soiav_read(int fd, void *buf, size_t count);
int soiav_write(int fd, const void *buf, size_t count);
int soiav_close(int fd);
off_t soiav_lseek(int fd, off_t offset, int whence);

// ==================== СТАНДАРТНАЯ БИБЛИОТЕКА ====================

// Строковые функции
size_t strlen(const char *s);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
char *strcat(char *dest, const char *src);
char *strncat(char *dest, const char *src, size_t n);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);

// Функции работы с памятью
void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

// Функции ввода/вывода
int putchar(int c);
int puts(const char *s);
int printf(const char *format, ...);
int sprintf(char *str, const char *format, ...);
int fprintf(FILE *stream, const char *format, ...);
int vprintf(const char *format, va_list ap);
int vsprintf(char *str, const char *format, va_list ap);

// Функции работы с файлами
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
void rewind(FILE *stream);
int fgetc(FILE *stream);
char *fgets(char *s, int size, FILE *stream);
int fputc(int c, FILE *stream);
int fputs(const char *s, FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);

// Функции времени
typedef long time_t;
struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

time_t time(time_t *t);
struct tm *localtime(const time_t *timep);
char *asctime(const struct tm *tm);
char *ctime(const time_t *timep);

// Математические функции
double sin(double x);
double cos(double x);
double tan(double x);
double sqrt(double x);
double pow(double x, double y);
double exp(double x);
double log(double x);
double log10(double x);
double fabs(double x);
double floor(double x);
double ceil(double x);

// Прочие функции
void exit(int status);
int atoi(const char *nptr);
long atol(const char *nptr);
double atof(const char *nptr);
int rand(void);
void srand(unsigned int seed);
void abort(void);
char *getenv(const char *name);

#endif
// userland/libc/soiav_libc.c - РЕАЛИЗАЦИЯ БИБЛИОТЕКИ
#include "soiav_libc.h"

// ==================== СИСТЕМНЫЕ ВЫЗОВЫ ====================

// Ассемблерная вставка для системных вызовов x86_64
long soiav_syscall(long number, ...) {
    long ret;
    va_list args;
    va_start(args, number);
    
    long a1 = va_arg(args, long);
    long a2 = va_arg(args, long);
    long a3 = va_arg(args, long);
    long a4 = va_arg(args, long);
    long a5 = va_arg(args, long);
    
    va_end(args);
    
    __asm__ volatile (
        "mov %1, %%rax\n"
        "mov %2, %%rdi\n"
        "mov %3, %%rsi\n"
        "mov %4, %%rdx\n"
        "mov %5, %%r10\n"
        "mov %6, %%r8\n"
        "mov %7, %%r9\n"
        "syscall\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"(number), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5)
        : "rax", "rdi", "rsi", "rdx", "r10", "r8", "r9", "memory"
    );
    
    return ret;
}

// Обертки для IPC системных вызовов
int soiav_ipc_create_queue(int flags) {
    return soiav_syscall(400, flags, 0, 0, 0, 0);
}

int soiav_ipc_send(int queue_id, unsigned long type, void *data, unsigned long size) {
    return soiav_syscall(401, queue_id, type, (long)data, size, 0);
}

int soiav_ipc_receive(int queue_id, unsigned long *type, void *buffer, unsigned long *size, int flags) {
    return soiav_syscall(402, queue_id, (long)type, (long)buffer, (long)size, flags);
}

int soiav_ipc_create_shm(unsigned long size, int flags) {
    return soiav_syscall(403, size, flags, 0, 0, 0);
}

void* soiav_ipc_attach(int shm_id) {
    return (void*)soiav_syscall(404, shm_id, 0, 0, 0, 0);
}

int soiav_ipc_detach(void *addr) {
    return soiav_syscall(405, (long)addr, 0, 0, 0, 0);
}

int soiav_ipc_create_sem(int initial, int max_val) {
    return soiav_syscall(406, initial, max_val, 0, 0, 0);
}

int soiav_ipc_sem_op(int sem_id, int op, int flags) {
    return soiav_syscall(407, sem_id, op, flags, 0, 0);
}

int soiav_ipc_close(int id) {
    return soiav_syscall(408, id, 0, 0, 0, 0);
}

// Обертки для GUI системных вызовов
int soiav_create_window(int width, int height, int flags, const char *title) {
    return soiav_syscall(420, width, height, flags, (long)title, 0);
}

int soiav_draw_rect(int window, int x, int y, int w, int h, unsigned int color) {
    return soiav_syscall(421, window, x, y, w, h, (long)color);
}

int soiav_draw_text(int window, int x, int y, const char *text, unsigned int color) {
    return soiav_syscall(422, window, x, y, (long)text, color);
}

int soiav_get_input(int window, void *event) {
    return soiav_syscall(423, window, (long)event, 0, 0, 0);
}

// Обертки для файловых системных вызовов
int soiav_open(const char *path, int flags, int mode) {
    return soiav_syscall(2, (long)path, flags, mode, 0, 0);
}

int soiav_read(int fd, void *buf, size_t count) {
    return soiav_syscall(0, fd, (long)buf, count, 0, 0);
}

int soiav_write(int fd, const void *buf, size_t count) {
    return soiav_syscall(1, fd, (long)buf, count, 0, 0);
}

int soiav_close(int fd) {
    return soiav_syscall(3, fd, 0, 0, 0, 0);
}

off_t soiav_lseek(int fd, off_t offset, int whence) {
    return soiav_syscall(8, fd, offset, whence, 0, 0);
}

// ==================== СТРОКОВЫЕ ФУНКЦИИ ====================

size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i]; i++)
        dest[i] = src[i];
    for (; i < n; i++)
        dest[i] = '\0';
    return dest;
}

char *strcat(char *dest, const char *src) {
    char *d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

char *strncat(char *dest, const char *src, size_t n) {
    char *d = dest;
    while (*d) d++;
    for (size_t i = 0; i < n && src[i]; i++)
        d[i] = src[i];
    d[n] = '\0';
    return dest;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i])
            return (unsigned char)s1[i] - (unsigned char)s2[i];
        if (!s1[i])
            break;
    }
    return 0;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == c)
            return (char*)s;
        s++;
    }
    return NULL;
}

char *strrchr(const char *s, int c) {
    const char *last = NULL;
    while (*s) {
        if (*s == c)
            last = s;
        s++;
    }
    return (char*)last;
}

char *strstr(const char *haystack, const char *needle) {
    size_t needle_len = strlen(needle);
    while (*haystack) {
        if (strncmp(haystack, needle, needle_len) == 0)
            return (char*)haystack;
        haystack++;
    }
    return NULL;
}

void *memcpy(void *dest, const void *src, size_t n) {
    char *d = dest;
    const char *s = src;
    for (size_t i = 0; i < n; i++)
        d[i] = s[i];
    return dest;
}

void *memmove(void *dest, const void *src, size_t n) {
    char *d = dest;
    const char *s = src;
    
    if (d < s) {
        for (size_t i = 0; i < n; i++)
            d[i] = s[i];
    } else {
        for (size_t i = n; i > 0; i--)
            d[i-1] = s[i-1];
    }
    return dest;
}

void *memset(void *s, int c, size_t n) {
    unsigned char *p = s;
    for (size_t i = 0; i < n; i++)
        p[i] = (unsigned char)c;
    return s;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = s1;
    const unsigned char *p2 = s2;
    
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i])
            return p1[i] - p2[i];
    }
    return 0;
}

// ==================== УПРАВЛЕНИЕ ПАМЯТЬЮ ====================

#define HEAP_SIZE (1024 * 1024)  // 1MB куча
static unsigned char heap[HEAP_SIZE];
static size_t heap_used = 0;

typedef struct mem_block {
    size_t size;
    int free;
    struct mem_block *next;
} mem_block_t;

static mem_block_t *free_list = NULL;

void init_heap(void) {
    free_list = (mem_block_t*)heap;
    free_list->size = HEAP_SIZE - sizeof(mem_block_t);
    free_list->free = 1;
    free_list->next = NULL;
}

void *malloc(size_t size) {
    if (!free_list) init_heap();
    
    // Выравнивание
    size = (size + 7) & ~7;
    
    mem_block_t *curr = free_list;
    while (curr) {
        if (curr->free && curr->size >= size) {
            if (curr->size > size + sizeof(mem_block_t) + 8) {
                // Разделяем блок
                mem_block_t *new_block = (mem_block_t*)((char*)curr + sizeof(mem_block_t) + size);
                new_block->size = curr->size - size - sizeof(mem_block_t);
                new_block->free = 1;
                new_block->next = curr->next;
                
                curr->size = size;
                curr->next = new_block;
            }
            curr->free = 0;
            return (void*)((char*)curr + sizeof(mem_block_t));
        }
        curr = curr->next;
    }
    
    return NULL;
}

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr)
        memset(ptr, 0, total);
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (!size) {
        free(ptr);
        return NULL;
    }
    
    mem_block_t *block = (mem_block_t*)((char*)ptr - sizeof(mem_block_t));
    size_t old_size = block->size;
    
    if (size <= old_size) return ptr;
    
    void *new_ptr = malloc(size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, old_size);
        free(ptr);
    }
    return new_ptr;
}

void free(void *ptr) {
    if (!ptr) return;
    
    mem_block_t *block = (mem_block_t*)((char*)ptr - sizeof(mem_block_t));
    block->free = 1;
    
    // Слияние с соседними свободными блоками
    mem_block_t *curr = free_list;
    while (curr) {
        if (curr->free && curr->next && curr->next->free) {
            curr->size += sizeof(mem_block_t) + curr->next->size;
            curr->next = curr->next->next;
        }
        curr = curr->next;
    }
}

// ==================== ФОРМАТИРОВАННЫЙ ВЫВОД ====================

static void print_unsigned(unsigned long long num, int base, FILE *stream) {
    char digits[] = "0123456789ABCDEF";
    char buffer[65];
    int i = 0;
    
    do {
        buffer[i++] = digits[num % base];
        num /= base;
    } while (num > 0);
    
    while (i > 0)
        fputc(buffer[--i], stream);
}

static void print_signed(long long num, int base, FILE *stream) {
    if (num < 0) {
        fputc('-', stream);
        num = -num;
    }
    print_unsigned(num, base, stream);
}

int vprintf_internal(FILE *stream, const char *format, va_list ap) {
    int count = 0;
    
    for (const char *p = format; *p; p++) {
        if (*p != '%') {
            fputc(*p, stream);
            count++;
            continue;
        }
        
        p++; // пропускаем '%'
        
        switch (*p) {
            case 'd': {
                int val = va_arg(ap, int);
                print_signed(val, 10, stream);
                break;
            }
            case 'u': {
                unsigned int val = va_arg(ap, unsigned int);
                print_unsigned(val, 10, stream);
                break;
            }
            case 'x': {
                unsigned int val = va_arg(ap, unsigned int);
                print_unsigned(val, 16, stream);
                break;
            }
            case 'c': {
                char val = (char)va_arg(ap, int);
                fputc(val, stream);
                break;
            }
            case 's': {
                char *val = va_arg(ap, char*);
                if (!val) val = "(null)";
                while (*val) {
                    fputc(*val++, stream);
                    count++;
                }
                continue;
            }
            case 'p': {
                void *val = va_arg(ap, void*);
                fputs("0x", stream);
                print_unsigned((unsigned long long)val, 16, stream);
                break;
            }
            case '%': {
                fputc('%', stream);
                break;
            }
            default:
                fputc('%', stream);
                fputc(*p, stream);
                break;
        }
        count++;
    }
    
    return count;
}

int printf(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vprintf_internal(stdout, format, ap);
    va_end(ap);
    return ret;
}

int sprintf(char *str, const char *format, ...) {
    // Упрощенная реализация
    va_list ap;
    va_start(ap, format);
    int ret = vsprintf(str, format, ap);
    va_end(ap);
    return ret;
}

int fprintf(FILE *stream, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vprintf_internal(stream, format, ap);
    va_end(ap);
    return ret;
}

int vsprintf(char *str, const char *format, va_list ap) {
    // Создаем временный файловый поток, пишущий в строку
    // Упрощенно - просто копируем
    char *p = str;
    for (const char *f = format; *f; f++)
        *p++ = *f;
    *p = '\0';
    return p - str;
}

// ==================== ФАЙЛОВЫЙ ВВОД/ВЫВОД ====================

#define FILE_BUFFER_SIZE 4096

static FILE _stdin = {0, NULL, 0, 0, 0, 0, 0, 0};
static FILE _stdout = {1, NULL, 0, 0, 0, 0, 0, 0};
static FILE _stderr = {2, NULL, 0, 0, 0, 0, 0, 0};

FILE *stdin = &_stdin;
FILE *stdout = &_stdout;
FILE *stderr = &_stderr;

FILE *fopen(const char *path, const char *mode) {
    FILE *stream = malloc(sizeof(FILE));
    if (!stream) return NULL;
    
    int flags = 0;
    if (mode[0] == 'r') {
        flags = O_RDONLY;
        if (mode[1] == '+') flags = O_RDWR;
    } else if (mode[0] == 'w') {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
        if (mode[1] == '+') flags = O_RDWR | O_CREAT | O_TRUNC;
    } else if (mode[0] == 'a') {
        flags = O_WRONLY | O_CREAT | O_APPEND;
        if (mode[1] == '+') flags = O_RDWR | O_CREAT | O_APPEND;
    } else {
        free(stream);
        return NULL;
    }
    
    int fd = soiav_open(path, flags, 0644);
    if (fd < 0) {
        free(stream);
        return NULL;
    }
    
    stream->fd = fd;
    stream->buf = malloc(FILE_BUFFER_SIZE);
    stream->buf_size = FILE_BUFFER_SIZE;
    stream->buf_pos = 0;
    stream->buf_len = 0;
    stream->flags = flags;
    stream->eof = 0;
    stream->error = 0;
    
    return stream;
}

int fclose(FILE *stream) {
    if (!stream) return EOF;
    
    // Сброс буфера
    if (stream->buf_pos > 0) {
        soiav_write(stream->fd, stream->buf, stream->buf_pos);
    }
    
    soiav_close(stream->fd);
    free(stream->buf);
    free(stream);
    return 0;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t total = size * nmemb;
    size_t read = 0;
    unsigned char *p = ptr;
    
    while (read < total) {
        if (stream->buf_pos >= stream->buf_len) {
            // Буфер пуст, читаем с диска
            stream->buf_len = soiav_read(stream->fd, stream->buf, stream->buf_size);
            stream->buf_pos = 0;
            
            if (stream->buf_len <= 0) {
                stream->eof = 1;
                break;
            }
        }
        
        size_t available = stream->buf_len - stream->buf_pos;
        size_t needed = total - read;
        size_t copy = (available < needed) ? available : needed;
        
        memcpy(p + read, stream->buf + stream->buf_pos, copy);
        read += copy;
        stream->buf_pos += copy;
    }
    
    return read / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t total = size * nmemb;
    const unsigned char *p = ptr;
    size_t written = 0;
    
    while (written < total) {
        size_t space = stream->buf_size - stream->buf_pos;
        size_t needed = total - written;
        size_t copy = (space < needed) ? space : needed;
        
        memcpy(stream->buf + stream->buf_pos, p + written, copy);
        written += copy;
        stream->buf_pos += copy;
        
        if (stream->buf_pos >= stream->buf_size) {
            // Буфер полный, сбрасываем на диск
            ssize_t ret = soiav_write(stream->fd, stream->buf, stream->buf_pos);
            if (ret < 0) {
                stream->error = 1;
                break;
            }
            stream->buf_pos = 0;
        }
    }
    
    return written / size;
}

int fseek(FILE *stream, long offset, int whence) {
    // Сброс буфера
    if (stream->buf_pos > 0) {
        soiav_write(stream->fd, stream->buf, stream->buf_pos);
        stream->buf_pos = 0;
    }
    stream->buf_len = 0;
    stream->eof = 0;
    
    return soiav_lseek(stream->fd, offset, whence);
}

long ftell(FILE *stream) {
    long pos = soiav_lseek(stream->fd, 0, SEEK_CUR);
    if (pos >= 0) {
        if (stream->buf_pos > 0) {
            // Корректировка для буферизованной записи
            pos += stream->buf_pos - stream->buf_len;
        }
    }
    return pos;
}

int fgetc(FILE *stream) {
    unsigned char c;
    if (fread(&c, 1, 1, stream) == 1)
        return c;
    return EOF;
}

char *fgets(char *s, int size, FILE *stream) {
    int i = 0;
    while (i < size - 1) {
        int c = fgetc(stream);
        if (c == EOF) break;
        s[i++] = c;
        if (c == '\n') break;
    }
    s[i] = '\0';
    return (i > 0) ? s : NULL;
}

int fputc(int c, FILE *stream) {
    unsigned char ch = c;
    if (fwrite(&ch, 1, 1, stream) == 1)
        return ch;
    return EOF;
}

int fputs(const char *s, FILE *stream) {
    size_t len = strlen(s);
    return (fwrite(s, 1, len, stream) == len) ? len : EOF;
}

int feof(FILE *stream) {
    return stream->eof;
}

int ferror(FILE *stream) {
    return stream->error;
}

// ==================== ПРОСТЕЙШИЕ ФУНКЦИИ ====================

int putchar(int c) {
    return fputc(c, stdout);
}

int puts(const char *s) {
    int ret = fputs(s, stdout);
    if (ret >= 0) {
        fputc('\n', stdout);
        ret++;
    }
    return ret;
}

void exit(int status) {
    // Системный вызов для завершения процесса
    soiav_syscall(60, status, 0, 0, 0, 0);
    while(1); // Никогда не выполнится
}

int atoi(const char *nptr) {
    int result = 0;
    int sign = 1;
    
    while (*nptr == ' ') nptr++;
    
    if (*nptr == '-') {
        sign = -1;
        nptr++;
    } else if (*nptr == '+') {
        nptr++;
    }
    
    while (*nptr >= '0' && *nptr <= '9') {
        result = result * 10 + (*nptr - '0');
        nptr++;
    }
    
    return sign * result;
}

long atol(const char *nptr) {
    return (long)atoi(nptr);
}

// Заглушки для математических функций
double sin(double x) { return x; }
double cos(double x) { return x; }
double tan(double x) { return x; }
double sqrt(double x) { return x; }
double pow(double x, double y) { return x; }
double exp(double x) { return x; }
double log(double x) { return x; }
double fabs(double x) { return x < 0 ? -x : x; }
double floor(double x) { return (int)x; }
double ceil(double x) { return (int)x + ((x > (int)x) ? 1 : 0); }

// Время
time_t time(time_t *t) {
    time_t now = soiav_syscall(201, 0, 0, 0, 0, 0);
    if (t) *t = now;
    return now;
}
