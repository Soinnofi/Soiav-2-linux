// kernel/drivers/input/soiav_input.c - ГЛАВНЫЙ ДРАЙВЕР ВВОДА
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/delay.h>

#define SOIAV_INPUT_VERSION "1.0"
#define MAX_DEVICES 32
#define KEYBOARD_BUFFER 256
#define MOUSE_BUFFER 128

// Структура устройства ввода
struct soiav_input_device {
    int id;
    char name[64];
    int type; // 0=клавиатура, 1=мышь, 2=тачпад, 3=джойстик
    int irq;
    void __iomem *base;
    struct input_dev *input;
    struct soiav_input_device *next;
    
    // Буферы событий
    struct {
        unsigned char keycode;
        int x, y;
        int wheel;
        unsigned int buttons;
    } buffer[128];
    int buffer_head;
    int buffer_tail;
    
    spinlock_t lock;
};

// Глобальный список устройств
static struct soiav_input_device *devices = NULL;
static int device_count = 0;

// Протокол PS/2 клавиатуры
#define PS2_DATA_PORT 0x60
#define PS2_STATUS_PORT 0x64
#define PS2_CMD_PORT 0x64

#define PS2_CMD_ENABLE 0xAE
#define PS2_CMD_DISABLE 0xAD
#define PS2_CMD_RESET 0xFF

// Таблица сканкодов -> ASCII
static unsigned char scancode_to_ascii[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    '-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// Инициализация PS/2 контроллера
static int init_ps2_controller(void) {
    unsigned char status;
    
    // Отключаем устройства
    outb(PS2_CMD_DISABLE, PS2_CMD_PORT);
    udelay(100);
    
    // Сбрасываем контроллер
    outb(PS2_CMD_RESET, PS2_CMD_PORT);
    udelay(1000);
    
    // Проверяем статус
    status = inb(PS2_STATUS_PORT);
    if (!(status & 0x20)) {
        printk(KERN_ERR "SoiavInput: PS/2 контроллер не отвечает\n");
        return -1;
    }
    
    // Включаем устройства
    outb(PS2_CMD_ENABLE, PS2_CMD_PORT);
    
    return 0;
}

// Обработчик прерываний клавиатуры
static irqreturn_t keyboard_interrupt(int irq, void *dev_id) {
    struct soiav_input_device *dev = (struct soiav_input_device*)dev_id;
    unsigned char scancode;
    unsigned long flags;
    
    // Читаем сканкод
    scancode = inb(PS2_DATA_PORT);
    
    spin_lock_irqsave(&dev->lock, flags);
    
    // Сохраняем в буфер
    dev->buffer[dev->buffer_head].keycode = scancode;
    dev->buffer_head = (dev->buffer_head + 1) % 128;
    
    // Отправляем событие в input subsystem
    input_report_key(dev->input, scancode & 0x7F, !(scancode & 0x80));
    input_sync(dev->input);
    
    spin_unlock_irqrestore(&dev->lock, flags);
    
    return IRQ_HANDLED;
}

// Обработчик прерываний мыши
static irqreturn_t mouse_interrupt(int irq, void *dev_id) {
    struct soiav_input_device *dev = (struct soiav_input_device*)dev_id;
    static unsigned char packet[3];
    static int packet_index = 0;
    unsigned char data;
    unsigned long flags;
    
    data = inb(PS2_DATA_PORT);
    
    spin_lock_irqsave(&dev->lock, flags);
    
    packet[packet_index++] = data;
    
    if (packet_index == 3) {
        // Полный пакет мыши получен
        int dx = (int)(packet[1]) - ((packet[0] << 4) & 0x100);
        int dy = (int)(packet[2]) - ((packet[0] << 3) & 0x100);
        int buttons = packet[0] & 0x07;
        
        // Сохраняем в буфер
        dev->buffer[dev->buffer_head].x = dx;
        dev->buffer[dev->buffer_head].y = -dy; // Инвертируем Y
        dev->buffer[dev->buffer_head].buttons = buttons;
        dev->buffer_head = (dev->buffer_head + 1) % 128;
        
        // Отправляем события
        input_report_rel(dev->input, REL_X, dx);
        input_report_rel(dev->input, REL_Y, -dy);
        input_report_key(dev->input, BTN_LEFT, buttons & 1);
        input_report_key(dev->input, BTN_RIGHT, buttons & 2);
        input_report_key(dev->input, BTN_MIDDLE, buttons & 4);
        input_sync(dev->input);
        
        packet_index = 0;
    }
    
    spin_unlock_irqrestore(&dev->lock, flags);
    
    return IRQ_HANDLED;
}

// Создание устройства клавиатуры
static struct soiav_input_device* create_keyboard_device(void) {
    struct soiav_input_device *dev;
    struct input_dev *input;
    
    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev) return NULL;
    
    input = input_allocate_device();
    if (!input) {
        kfree(dev);
        return NULL;
    }
    
    dev->id = device_count++;
    dev->type = 0;
    strcpy(dev->name, "Soiav Keyboard");
    dev->irq = 1; // IRQ1 для клавиатуры
    dev->input = input;
    spin_lock_init(&dev->lock);
    
    // Настройка input устройства
    input->name = dev->name;
    input->id.bustype = BUS_I8042;
    input->id.vendor = 0x534F; // "SO"
    input->id.product = 0x4941; // "IA"
    input->id.version = 0x0200;
    
    // Устанавливаем поддерживаемые клавиши
    __set_bit(EV_KEY, input->evbit);
    __set_bit(EV_REP, input->evbit);
    
    for (int i = 0; i < 128; i++) {
        if (scancode_to_ascii[i]) {
            __set_bit(i, input->keybit);
        }
    }
    
    // Регистрируем обработчик прерывания
    if (request_irq(dev->irq, keyboard_interrupt, IRQF_SHARED, 
                    "soiav-keyboard", dev)) {
        input_free_device(input);
        kfree(dev);
        return NULL;
    }
    
    input_register_device(input);
    
    return dev;
}

// Создание устройства мыши
static struct soiav_input_device* create_mouse_device(void) {
    struct soiav_input_device *dev;
    struct input_dev *input;
    
    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev) return NULL;
    
    input = input_allocate_device();
    if (!input) {
        kfree(dev);
        return NULL;
    }
    
    dev->id = device_count++;
    dev->type = 1;
    strcpy(dev->name, "Soiav Mouse");
    dev->irq = 12; // IRQ12 для мыши
    dev->input = input;
    spin_lock_init(&dev->lock);
    
    // Настройка input устройства
    input->name = dev->name;
    input->id.bustype = BUS_I8042;
    input->id.vendor = 0x534F;
    input->id.product = 0x4D4F; // "MO"
    input->id.version = 0x0200;
    
    // Устанавливаем поддерживаемые события
    __set_bit(EV_KEY, input->evbit);
    __set_bit(EV_REL, input->evbit);
    
    __set_bit(BTN_LEFT, input->keybit);
    __set_bit(BTN_RIGHT, input->keybit);
    __set_bit(BTN_MIDDLE, input->keybit);
    
    __set_bit(REL_X, input->relbit);
    __set_bit(REL_Y, input->relbit);
    __set_bit(REL_WHEEL, input->relbit);
    
    // Регистрируем обработчик прерывания
    if (request_irq(dev->irq, mouse_interrupt, IRQF_SHARED,
                    "soiav-mouse", dev)) {
        input_free_device(input);
        kfree(dev);
        return NULL;
    }
    
    input_register_device(input);
    
    return dev;
}

// Инициализация подсистемы ввода
static int __init soiav_input_init(void) {
    printk(KERN_INFO "Soiav Input: Инициализация драйверов ввода v%s\n", 
           SOIAV_INPUT_VERSION);
    
    // Инициализация PS/2 контроллера
    if (init_ps2_controller() < 0) {
        printk(KERN_ERR "Soiav Input: Не удалось инициализировать PS/2\n");
        return -1;
    }
    
    // Создаем клавиатуру
    struct soiav_input_device *kbd = create_keyboard_device();
    if (kbd) {
        kbd->next = devices;
        devices = kbd;
        printk(KERN_INFO "Soiav Input: Клавиатура инициализирована\n");
    }
    
    // Создаем мышь
    struct soiav_input_device *mouse = create_mouse_device();
    if (mouse) {
        mouse->next = devices;
        devices = mouse;
        printk(KERN_INFO "Soiav Input: Мышь инициализирована\n");
    }
    
    printk(KERN_INFO "Soiav Input: Готово, устройств: %d\n", device_count);
    return 0;
}

// API для пользовательского пространства
int soiav_read_keyboard(char *buffer, int max) {
    struct soiav_input_device *dev;
    int count = 0;
    unsigned long flags;
    
    // Ищем клавиатуру
    for (dev = devices; dev; dev = dev->next) {
        if (dev->type == 0) break;
    }
    
    if (!dev) return -1;
    
    spin_lock_irqsave(&dev->lock, flags);
    
    while (dev->buffer_tail != dev->buffer_head && count < max - 1) {
        unsigned char scancode = dev->buffer[dev->buffer_tail].keycode;
        unsigned char ascii = scancode_to_ascii[scancode & 0x7F];
        
        if (ascii && !(scancode & 0x80)) {
            buffer[count++] = ascii;
        }
        
        dev->buffer_tail = (dev->buffer_tail + 1) % 128;
    }
    
    spin_unlock_irqrestore(&dev->lock, flags);
    
    buffer[count] = '\0';
    return count;
}

int soiav_read_mouse(int *dx, int *dy, int *buttons) {
    struct soiav_input_device *dev;
    unsigned long flags;
    
    // Ищем мышь
    for (dev = devices; dev; dev = dev->next) {
        if (dev->type == 1) break;
    }
    
    if (!dev) return -1;
    
    spin_lock_irqsave(&dev->lock, flags);
    
    if (dev->buffer_tail != dev->buffer_head) {
        *dx = dev->buffer[dev->buffer_tail].x;
        *dy = dev->buffer[dev->buffer_tail].y;
        *buttons = dev->buffer[dev->buffer_tail].buttons;
        dev->buffer_tail = (dev->buffer_tail + 1) % 128;
        spin_unlock_irqrestore(&dev->lock, flags);
        return 1;
    }
    
    spin_unlock_irqrestore(&dev->lock, flags);
    return 0;
}

// Экспорт API
EXPORT_SYMBOL(soiav_read_keyboard);
EXPORT_SYMBOL(soiav_read_mouse);

module_init(soiav_input_init);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Soiav Systems");
MODULE_DESCRIPTION("Soiav OS 2 Input Drivers");
