// kernel/drivers/sound/soiav_audio.c - ЗВУКОВАЯ ПОДСИСТЕМА
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/sound.h>
#include <linux/soundcard.h>
#include <linux/delay.h>
#include <linux/slab.h>

#define SOIAV_AUDIO_VERSION "1.0"
#define SAMPLE_RATE 44100
#define BUFFER_SIZE 8192
#define CHANNELS 2

// Структура звуковой карты
struct soiav_audio_device {
    int id;
    char name[64];
    void __iomem *mmio;
    int irq;
    
    // DMA буферы
    void *dma_buffer;
    dma_addr_t dma_handle;
    int buffer_size;
    
    // Воспроизведение
    struct {
        int active;
        int volume;
        int sample_rate;
        int channels;
        int bits;
        void *buffer;
        int buffer_pos;
        int buffer_len;
        spinlock_t lock;
    } playback;
    
    // Запись
    struct {
        int active;
        int volume;
        void *buffer;
        int buffer_pos;
        spinlock_t lock;
    } capture;
    
    struct soiav_audio_device *next;
};

static struct soiav_audio_device *audio_devices = NULL;

// Эмуляция Intel HD Audio (ICH6/ICH7/ICH8)
#define HDA_PCI_VENDOR 0x8086  // Intel
#define HDA_PCI_DEVICE 0x2668   // ICH6

#define HDA_REG_GCAP 0x00
#define HDA_REG_VMIN 0x02
#define HDA_REG_VMAJ 0x03
#define HDA_REG_OUTPAY 0x04
#define HDA_REG_INPAY 0x06
#define HDA_REG_GCTL 0x08
#define HDA_REG_WAKEEN 0x0C
#define HDA_REG_STATESTS 0x0E
#define HDA_REG_INTCTL 0x20
#define HDA_REG_INTSTS 0x24

#define HDA_CMD_OFFSET 0x60
#define HDA_RESP_OFFSET 0x64
#define HDA_DPLBASE 0x70
#define HDA_DPUBASE 0x74

// Генерация звука (синусоида)
static void generate_sine_wave(short *buffer, int samples, int freq, int volume) {
    for (int i = 0; i < samples; i++) {
        double t = (double)i / SAMPLE_RATE;
        short val = (short)(volume * 32767 * sin(2 * M_PI * freq * t));
        
        // Левый канал
        buffer[i * 2] = val;
        // Правый канал
        buffer[i * 2 + 1] = val;
    }
}

// Генерация звука (квадратная волна - для звуков системы)
static void generate_square_wave(short *buffer, int samples, int freq, int volume) {
    for (int i = 0; i < samples; i++) {
        double t = (double)i / SAMPLE_RATE;
        short val = (sin(2 * M_PI * freq * t) > 0) ? 
                    (short)(volume * 32767) : 
                    (short)(-volume * 32767);
        
        buffer[i * 2] = val;
        buffer[i * 2 + 1] = val;
    }
}

// Генерация звука (пила)
static void generate_saw_wave(short *buffer, int samples, int freq, int volume) {
    for (int i = 0; i < samples; i++) {
        double t = (double)i / SAMPLE_RATE;
        double phase = fmod(t * freq, 1.0);
        short val = (short)(volume * 32767 * (2 * phase - 1));
        
        buffer[i * 2] = val;
        buffer[i * 2 + 1] = val;
    }
}

// Инициализация DMA
static int init_audio_dma(struct soiav_audio_device *dev) {
    dev->dma_buffer = dma_alloc_coherent(NULL, BUFFER_SIZE * 4,
                                         &dev->dma_handle, GFP_KERNEL);
    if (!dev->dma_buffer) {
        printk(KERN_ERR "SoiavAudio: Не удалось выделить DMA буфер\n");
        return -1;
    }
    
    dev->buffer_size = BUFFER_SIZE * 4;
    memset(dev->dma_buffer, 0, dev->buffer_size);
    
    return 0;
}

// Обработчик прерываний звука
static irqreturn_t audio_interrupt(int irq, void *dev_id) {
    struct soiav_audio_device *dev = (struct soiav_audio_device*)dev_id;
    unsigned long flags;
    
    spin_lock_irqsave(&dev->playback.lock, flags);
    
    if (dev->playback.active) {
        // Заполняем следующую порцию данных
        if (dev->playback.buffer_pos < dev->playback.buffer_len) {
            int remain = dev->playback.buffer_len - dev->playback.buffer_pos;
            int copy = (remain > BUFFER_SIZE) ? BUFFER_SIZE : remain;
            
            memcpy(dev->dma_buffer, 
                   dev->playback.buffer + dev->playback.buffer_pos * 4,
                   copy * 4);
            
            dev->playback.buffer_pos += copy;
        } else {
            // Воспроизведение завершено
            dev->playback.active = 0;
        }
    }
    
    spin_unlock_irqrestore(&dev->playback.lock, flags);
    
    return IRQ_HANDLED;
}

// Системные звуки
void soiav_play_system_sound(int sound_type) {
    struct soiav_audio_device *dev = audio_devices;
    short *buffer;
    int samples = SAMPLE_RATE / 4; // 0.25 секунды
    
    if (!dev || !dev->playback.active) return;
    
    buffer = kmalloc(samples * CHANNELS * sizeof(short), GFP_KERNEL);
    if (!buffer) return;
    
    switch(sound_type) {
        case 0: // Звук загрузки
            generate_sine_wave(buffer, samples, 440, 50); // Ля
            break;
        case 1: // Звук уведомления
            generate_square_wave(buffer, samples/4, 880, 30); // Ля (октава)
            break;
        case 2: // Звук ошибки
            generate_saw_wave(buffer, samples, 220, 40); // Тревожный
            break;
        case 3: // Звук закрытия
            generate_sine_wave(buffer, samples/2, 330, 35); // Ми
            break;
    }
    
    spin_lock(&dev->playback.lock);
    dev->playback.buffer = buffer;
    dev->playback.buffer_len = samples;
    dev->playback.buffer_pos = 0;
    spin_unlock(&dev->playback.lock);
    
    kfree(buffer);
}
EXPORT_SYMBOL(soiav_play_system_sound);

// Воспроизведение WAV файла
int soiav_play_wav(void *wav_data, int size) {
    struct soiav_audio_device *dev = audio_devices;
    
    if (!dev || !dev->playback.active) return -1;
    
    // Пропускаем WAV заголовок (44 байта)
    short *audio_data = (short*)(wav_data + 44);
    int audio_samples = (size - 44) / (CHANNELS * sizeof(short));
    
    spin_lock(&dev->playback.lock);
    dev->playback.buffer = audio_data;
    dev->playback.buffer_len = audio_samples;
    dev->playback.buffer_pos = 0;
    spin_unlock(&dev->playback.lock);
    
    return 0;
}
EXPORT_SYMBOL(soiav_play_wav);

// Установка громкости
void soiav_set_volume(int volume) {
    struct soiav_audio_device *dev;
    
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    
    for (dev = audio_devices; dev; dev = dev->next) {
        dev->playback.volume = volume;
        dev->capture.volume = volume;
    }
}
EXPORT_SYMBOL(soiav_set_volume);

// Инициализация звуковой карты
static int soiav_audio_probe(struct pci_dev *pdev, 
                             const struct pci_device_id *id) {
    struct soiav_audio_device *dev;
    int ret;
    
    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev) return -ENOMEM;
    
    ret = pci_enable_device(pdev);
    if (ret) {
        kfree(dev);
        return ret;
    }
    
    dev->id = pdev->devfn;
    sprintf(dev->name, "Soiav HDA %02x:%02x.%d", 
            pdev->bus->number, PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));
    
    // Получаем MMIO
    dev->mmio = pci_iomap(pdev, 0, 0);
    if (!dev->mmio) {
        pci_disable_device(pdev);
        kfree(dev);
        return -ENOMEM;
    }
    
    dev->irq = pdev->irq;
    
    // Инициализация DMA
    if (init_audio_dma(dev) < 0) {
        pci_iounmap(pdev, dev->mmio);
        pci_disable_device(pdev);
        kfree(dev);
        return -ENOMEM;
    }
    
    spin_lock_init(&dev->playback.lock);
    spin_lock_init(&dev->capture.lock);
    
    dev->playback.active = 1;
    dev->playback.volume = 80;
    dev->playback.sample_rate = SAMPLE_RATE;
    dev->playback.channels = CHANNELS;
    dev->playback.bits = 16;
    
    // Регистрируем прерывание
    ret = request_irq(dev->irq, audio_interrupt, IRQF_SHARED,
                      "soiav-audio", dev);
    if (ret) {
        dma_free_coherent(NULL, dev->buffer_size, 
                          dev->dma_buffer, dev->dma_handle);
        pci_iounmap(pdev, dev->mmio);
        pci_disable_device(pdev);
        kfree(dev);
        return ret;
    }
    
    // Добавляем в список
    dev->next = audio_devices;
    audio_devices = dev;
    
    printk(KERN_INFO "SoiavAudio: Звуковая карта %s инициализирована\n", 
           dev->name);
    
    // Тестовый звук
    soiav_play_system_sound(0);
    
    return 0;
}

// Удаление устройства
static void soiav_audio_remove(struct pci_dev *pdev) {
    struct soiav_audio_device *dev = audio_devices;
    struct soiav_audio_device *prev = NULL;
    
    while (dev) {
        if (dev->mmio == pci_iomap(pdev, 0, 0)) {
            if (prev)
                prev->next = dev->next;
            else
                audio_devices = dev->next;
            
            free_irq(dev->irq, dev);
            dma_free_coherent(NULL, dev->buffer_size,
                              dev->dma_buffer, dev->dma_handle);
            pci_iounmap(pdev, dev->mmio);
            pci_disable_device(pdev);
            kfree(dev);
            break;
        }
        prev = dev;
        dev = dev->next;
    }
}

// Таблица PCI ID
static const struct pci_device_id soiav_audio_ids[] = {
    { PCI_DEVICE(HDA_PCI_VENDOR, HDA_PCI_DEVICE) },
    { 0, }
};

MODULE_DEVICE_TABLE(pci, soiav_audio_ids);

static struct pci_driver soiav_audio_driver = {
    .name = "soiav-audio",
    .id_table = soiav_audio_ids,
    .probe = soiav_audio_probe,
    .remove = soiav_audio_remove,
};

// Инициализация модуля
static int __init soiav_audio_init(void) {
    printk(KERN_INFO "Soiav Audio: Загрузка звуковой подсистемы v%s\n",
           SOIAV_AUDIO_VERSION);
    
    return pci_register_driver(&soiav_audio_driver);
}

// Выгрузка модуля
static void __exit soiav_audio_exit(void) {
    pci_unregister_driver(&soiav_audio_driver);
    printk(KERN_INFO "Soiav Audio: Выгружен\n");
}

module_init(soiav_audio_init);
module_exit(soiav_audio_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Soiav Systems");
MODULE_DESCRIPTION("Soiav OS 2 Audio Driver");
