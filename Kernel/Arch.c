// arch/x86/soiav_arch.c - ПОЛНАЯ АРХИТЕКТУРА Soiav OS 2
// Один файл содержит всё для работы с процессором

#include <linux/types.h>
#include <linux/string.h>

// ========== 1. ЗАГРУЗКА (16-битный режим) ==========
asm(
    ".code16\n"
    ".section .text\n"
    "_start:\n"
    "    jmp real_start\n"
    ".org 0x1fe\n"
    "    .word 0xaa55\n"           // Сигнатура загрузки
    "real_start:\n"
    "    cli\n"                      // Отключаем прерывания
    "    xorw %ax, %ax\n"
    "    movw %ax, %ds\n"
    "    movw %ax, %es\n"
    "    movw %ax, %ss\n"
    "    movw $0x7c00, %sp\n"        // Стек
    "    movw $welcome_msg, %si\n"
    "    call print\n"
    "    lgdt gdt_desc\n"             // Загружаем GDT
    "    movl %cr0, %eax\n"
    "    orb $1, %al\n"               // Включаем защищенный режим
    "    movl %eax, %cr0\n"
    "    ljmp $0x08, $protected_mode\n"
    "print:\n"
    "    lodsb\n"
    "    orb %al, %al\n"
    "    jz 1f\n"
    "    movb $0x0e, %ah\n"
    "    int $0x10\n"
    "    jmp print\n"
    "1:  ret\n"
    "welcome_msg: .string 'Soiav OS 2 загружается...'\n"
    "gdt:\n"
    "    .quad 0\n"                   // NULL дескриптор
    "    .quad 0x00cf9a000000ffff\n"  // Кодовый сегмент
    "    .quad 0x00cf92000000ffff\n"  // Данные сегмент
    "gdt_desc:\n"
    "    .word 23\n"
    "    .long gdt\n"
);

// ========== 2. ЗАЩИЩЕННЫЙ РЕЖИМ (32-бита) ==========
void __attribute__((section(".text32"))) protected_mode(void) {
    unsigned char* video = (unsigned char*)0xb8000;
    const char* msg = "32-битный режим OK -> проверка 64-бит...";
    
    // Очистка экрана
    for(int i = 0; i < 80*25*2; i+=2) {
        video[i] = ' ';
        video[i+1] = 0x07;
    }
    
    // Вывод сообщения
    for(int i = 0; msg[i]; i++) {
        video[i*2] = msg[i];
        video[i*2+1] = 0x0a;  // Зеленый цвет
    }
    
    // Проверка поддержки 64-бит
    unsigned int eax, ebx, ecx, edx;
    
    // CPUID
    eax = 0x80000000;
    asm volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    
    if(eax >= 0x80000001) {
        eax = 0x80000001;
        asm volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
        
        if(edx & (1 << 29)) {  // Бит 29 = long mode
            const char* ok = "64-бита поддерживается! Вход...";
            for(int i = 0; ok[i]; i++) {
                video[(i+50)*2] = ok[i];
                video[(i+50)*2+1] = 0x0a;
            }
            enter_long_mode();
        }
    }
}

// ========== 3. ПЕРЕХОД В 64-БИТА ==========
void enter_long_mode(void) {
    // Настройка страничной памяти
    unsigned long pml4[512] __attribute__((aligned(4096)));
    unsigned long pdpt[512] __attribute__((aligned(4096)));
    unsigned long pd[512] __attribute__((aligned(4096)));
    
    // Очистка таблиц
    for(int i = 0; i < 512; i++) {
        pml4[i] = 0;
        pdpt[i] = 0;
        pd[i] = 0;
    }
    
    // Отображение первых 2MB памяти
    pml4[0] = (unsigned long)pdpt | 0x03;
    pdpt[0] = (unsigned long)pd | 0x03;
    
    for(int i = 0; i < 512; i++) {
        pd[i] = (i * 0x200000) | 0x83;  // 2MB страницы
    }
    
    // Включение PAE
    asm volatile("mov %%cr4, %%rax; or $(1 << 5), %%rax; mov %%rax, %%cr4" ::: "rax");
    
    // Загрузка PML4
    asm volatile("mov %0, %%cr3" : : "r" (pml4));
    
    // Включение long mode
    unsigned long efer;
    asm volatile(
        "mov $0xc0000080, %%ecx; rdmsr; mov %%eax, %0"
        : "=r" (efer) : : "eax", "ecx", "edx"
    );
    efer |= (1 << 8);  // Бит LME
    asm volatile(
        "mov $0xc0000080, %%ecx; mov %0, %%eax; xor %%edx, %%edx; wrmsr"
        : : "r" (efer) : "eax", "ecx", "edx"
    );
    
    // Включение страничной адресации
    asm volatile(
        "mov %%cr0, %%rax; or $(1 << 31), %%rax; mov %%rax, %%cr0"
        ::: "rax"
    );
    
    // Прыжок в 64-бита
    asm volatile(
        "push $0x08\n"
        "push $long_mode\n"
        "retfq\n"
        "long_mode:\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%ss\n"
        ::: "rax"
    );
}

// ========== 4. 64-БИТНЫЙ РЕЖИМ ==========
void __attribute__((section(".text64"))) start_soiav_kernel(void) {
    // Здесь уже чисто 64-битный код
    unsigned long* framebuffer = (unsigned long*)0xffffffff80000000;
    
    // Инициализация всех компонентов
    init_gdt_64bit();     // Таблицы дескрипторов
    init_idt_64bit();     // Таблицы прерываний
    init_tss_64bit();     // Сегменты задач
    init_syscalls();      // Системные вызовы
    
    // Переход к основному ядру
    soiav_main();
}

// ========== 5. ТАБЛИЦЫ ДЕСКРИПТОРОВ ==========
struct gdt_entry {
    unsigned short limit_low;
    unsigned short base_low;
    unsigned char base_middle;
    unsigned char access;
    unsigned char limit_high : 4;
    unsigned char flags : 4;
    unsigned char base_high;
} __attribute__((packed));

struct gdt_ptr {
    unsigned short limit;
    unsigned long long base;
} __attribute__((packed));

void init_gdt_64bit(void) {
    static struct gdt_entry gdt[7];
    static struct gdt_ptr gp;
    
    // NULL дескриптор
    gdt[0].limit_low = 0;
    gdt[0].base_low = 0;
    gdt[0].base_middle = 0;
    gdt[0].access = 0;
    gdt[0].limit_high = 0;
    gdt[0].flags = 0;
    gdt[0].base_high = 0;
    
    // Кодовый сегмент ядра
    gdt[1].limit_low = 0;
    gdt[1].base_low = 0;
    gdt[1].base_middle = 0;
    gdt[1].access = 0x9a;  // Присутствует, исполняемый, чтение
    gdt[1].limit_high = 0;
    gdt[1].flags = 0x2;    // 64-бита
    gdt[1].base_high = 0;
    
    // Данные сегмент ядра
    gdt[2].limit_low = 0;
    gdt[2].base_low = 0;
    gdt[2].base_middle = 0;
    gdt[2].access = 0x92;  // Присутствует, данные, запись
    gdt[2].limit_high = 0;
    gdt[2].flags = 0x0;
    gdt[2].base_high = 0;
    
    gp.limit = sizeof(gdt) - 1;
    gp.base = (unsigned long long)&gdt;
    
    asm volatile("lgdt %0" : : "m" (gp));
}

// ========== 6. ПРЕРЫВАНИЯ ==========
struct idt_entry {
    unsigned short base_low;
    unsigned short selector;
    unsigned char ist;
    unsigned char flags;
    unsigned short base_middle;
    unsigned int base_high;
    unsigned int zero;
} __attribute__((packed));

struct idt_ptr {
    unsigned short limit;
    unsigned long long base;
} __attribute__((packed));

void init_idt_64bit(void) {
    static struct idt_entry idt[256];
    static struct idt_ptr ip;
    
    for(int i = 0; i < 256; i++) {
        idt[i].base_low = 0;
        idt[i].selector = 0;
        idt[i].ist = 0;
        idt[i].flags = 0;
        idt[i].base_middle = 0;
        idt[i].base_high = 0;
        idt[i].zero = 0;
    }
    
    // Обработчик прерываний по умолчанию
    void* default_handler = soiav_default_int;
    
    for(int i = 0; i < 32; i++) {
        unsigned long long handler = (unsigned long long)default_handler;
        idt[i].base_low = handler & 0xffff;
        idt[i].base_middle = (handler >> 16) & 0xffff;
        idt[i].base_high = (handler >> 32) & 0xffffffff;
        idt[i].selector = 0x08;  // Сегмент кода
        idt[i].ist = 0;
        idt[i].flags = 0x8e;     // Присутствует, шлюз прерываний
        idt[i].zero = 0;
    }
    
    ip.limit = sizeof(idt) - 1;
    ip.base = (unsigned long long)&idt;
    
    asm volatile("lidt %0" : : "m" (ip));
}

// Обработчик по умолчанию
void soiav_default_int(void) {
    asm volatile(
        "push %rax\n"
        "push %rbx\n"
        "push %rcx\n"
        "push %rdx\n"
        "mov $0x0e, %ah\n"     // Красный цвет
        "mov $'!', %al\n"
        "mov $0xb8000, %rbx\n"
        "mov %ax, (%rbx)\n"
        "pop %rdx\n"
        "pop %rcx\n"
        "pop %rbx\n"
        "pop %rax\n"
        "iretq\n"
    );
}

// ========== 7. СИСТЕМНЫЕ ВЫЗОВЫ ==========
#define SYSCALL_MAX 512

typedef long (*syscall_t)(long, long, long, long, long);
syscall_t syscall_table[SYSCALL_MAX];

void init_syscalls(void) {
    // Регистрация системных вызовов
    syscall_table[0] = soiav_exit;
    syscall_table[1] = soiav_open;
    syscall_table[2] = soiav_read;
    syscall_table[3] = soiav_write;
    syscall_table[4] = soiav_close;
    syscall_table[5] = soiav_create_window;  // Свой вызов
    
    // Настройка MSR для syscall/sysret
    unsigned long long star = 0;
    star |= (unsigned long long)0x08 << 32;    // Сегмент ядра
    star |= (unsigned long long)0x10 << 48;    // Сегмент пользователя
    
    asm volatile(
        "wrmsr" : : "c" (0xc0000081), "a" (star & 0xffffffff), "d" (star >> 32)
    );
    
    // Указатель на обработчик
    asm volatile(
        "wrmsr" : : "c" (0xc0000082), 
                    "a" ((unsigned long long)soiav_syscall_entry & 0xffffffff),
                    "d" ((unsigned long long)soiav_syscall_entry >> 32)
    );
}

// Обработчик системных вызовов
void soiav_syscall_entry(void) {
    asm volatile(
        "swapgs\n"
        "mov %rsp, %gs:8\n"
        "mov %gs:0, %rsp\n"
        "push %rax\n"
        "push %rcx\n"
        "push %rdx\n"
        "push %rsi\n"
        "push %rdi\n"
        "push %r8\n"
        "push %r9\n"
        "push %r10\n"
        "push %r11\n"
        
        // Вызов функции из таблицы
        "cmp $512, %rax\n"
        "jae 1f\n"
        "mov syscall_table(,%rax,8), %rbx\n"
        "test %rbx, %rbx\n"
        "jz 1f\n"
        "call *%rbx\n"
        "jmp 2f\n"
        "1: mov $-1, %rax\n"  // Ошибка
        "2:\n"
        
        "pop %r11\n"
        "pop %r10\n"
        "pop %r9\n"
        "pop %r8\n"
        "pop %rdi\n"
        "pop %rsi\n"
        "pop %rdx\n"
        "pop %rcx\n"
        "pop %rbx\n"
        "mov %gs:8, %rsp\n"
        "swapgs\n"
        "sysretq\n"
    );
}

// ========== 8. УПРАВЛЕНИЕ ПАМЯТЬЮ ==========
#define PAGE_SIZE 4096
#define KERNEL_VIRT_BASE 0xffffffff80000000

struct page {
    unsigned long flags;
    unsigned long count;
    struct page *next;
};

static struct page *free_pages = NULL;

void init_memory(unsigned long memory_start, unsigned long memory_end) {
    // Создание списка свободных страниц
    for(unsigned long addr = memory_start; addr < memory_end; addr += PAGE_SIZE) {
        struct page *p = (struct page *)addr;
        p->flags = 0;
        p->count = 0;
        p->next = free_pages;
        free_pages = p;
    }
}

void* alloc_page(void) {
    if(!free_pages) return NULL;
    
    struct page *p = free_pages;
    free_pages = p->next;
    p->flags = 1;
    p->count = 1;
    
    return (void*)p;
}

void free_page(void *addr) {
    struct page *p = (struct page*)addr;
    p->flags = 0;
    p->count = 0;
    p->next = free_pages;
    free_pages = p;
}

// ========== 9. ГЛАВНЫЙ ВХОД ==========
void soiav_main(void) {
    // Инициализация всей архитектуры
    unsigned char* video = (unsigned char*)0xb8000;
    const char* msg = "Soiav OS 2 x86_64 Architecture готов!";
    
    // Вывод сообщения
    for(int i = 0; msg[i]; i++) {
        video[i*2] = msg[i];
        video[i*2+1] = 0x2f;  // Зеленый на зеленом
    }
    
    // Инициализация памяти
    init_memory(0x100000, 0x8000000);  // 128MB
    
    // Вход в главный цикл
    while(1) {
        asm volatile("hlt");  // Ждем прерывания
    }
}

// ========== 10. РЕАЛЬНЫЕ СИСТЕМНЫЕ ВЫЗОВЫ ==========
long soiav_exit(long code, long a2, long a3, long a4, long a5) {
    // Завершение процесса
    asm volatile("hlt");
    return 0;
}

long soiav_open(long path, long flags, long mode, long a4, long a5) {
    // Открытие файла
    return -1;  // Заглушка
}

long soiav_read(long fd, long buf, long count, long a4, long a5) {
    // Чтение файла
    return -1;  // Заглушка
}

long soiav_write(long fd, long buf, long count, long a4, long a5) {
    // Запись файла
    return -1;  // Заглушка
}

long soiav_close(long fd, long a2, long a3, long a4, long a5) {
    // Закрытие файла
    return -1;  // Заглушка
}

// СВОЙ СИСТЕМНЫЙ ВЫЗОВ - создание окна
long soiav_create_window(long width, long height, long flags, long title, long a5) {
    unsigned char* video = (unsigned char*)0xb8000;
    const char* win_title = (const char*)title;
    
    // Рисуем окно в текстовом режиме
    for(int y = 0; y < height && y < 25; y++) {
        for(int x = 0; x < width && x < 80; x++) {
            int pos = y * 160 + x * 2;
            video[pos] = ' ';
            video[pos+1] = 0x1f;  // Синий фон
        }
    }
    
    return 1;  // ID окна
}
