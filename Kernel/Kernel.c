// kernel/soiav_main.c - Главный файл ядра Soiav
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/soiav/soiav_api.h>

#define SOIAV_VERSION "2.0.0"
#define SOIAV_CODENAME "Soiav OS"

// Собственный системный вызов для GUI
asmlinkage long sys_soiav_gui_operation(struct soiav_gui_request __user *req) {
    struct soiav_gui_request request;
    struct task_struct *current_task = current;
    
    if (copy_from_user(&request, req, sizeof(request)))
        return -EFAULT;
    
    switch(request.operation) {
        case SOIAV_CREATE_WINDOW:
            return soiav_create_window(request.pid, &request.params);
            
        case SOIAV_ANIMATE_WINDOW:
            return soiav_animate_window(request.window_id, 
                                        request.animation_type,
                                        request.duration);
                                        
        case SOIAV_RENDER_FRAME:
            return soiav_render_frame(request.window_id, 
                                      request.frame_data);
                                      
        case SOIAV_BLUR_BACKGROUND:
            return soiav_apply_blur(request.window_id, 
                                     request.blur_radius);
    }
    
    return -EINVAL;
}

// Собственный планировщик для плавных анимаций
void soiav_scheduler_tick(struct rq *rq) {
    struct task_struct *p = rq->curr;
    
    // Приоритет для GUI приложений
    if (p->soiav_flags & SOIAF_GUI_APP) {
        p->static_prio = MAX_RT_PRIO - 5;
        p->sched_class = &rt_sched_class;
    }
    
    // Проверка анимаций
    if (p->soiav_animations > 0) {
        // Увеличиваем квант времени для плавности
        p->time_slice += 2;
    }
}

// Собственная файловая система
static struct file_system_type soiav_fs_type = {
    .owner = THIS_MODULE,
    .name = "soiavfs",
    .mount = soiav_mount,
    .kill_sb = soiav_kill_superblock,
};

// Инициализация ядра
static int __init soiav_kernel_init(void) {
    printk(KERN_INFO "Soiav OS 2: Запуск ядра версии %s\n", SOIAV_VERSION);
    
    // Регистрация системных вызовов
    register_syscall(__NR_soiav_gui, sys_soiav_gui_operation);
    
    // Инициализация графической подсистемы
    soiav_gpu_init();
    
    // Загрузка собственных драйверов
    soiav_driver_init();
    
    // Монтирование SoiavFS
    register_filesystem(&soiav_fs_type);
    
    return 0;
}

module_init(soiav_kernel_init);
