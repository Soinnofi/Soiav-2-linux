// display-server/display_server.c - ПОЛНЫЙ ДИСПЛЕЙНЫЙ СЕРВЕР Soiav OS 2
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include <linux/fb.h>
#include <linux/input.h>

#define SOIAV_DISPLAY_VERSION "2.0"
#define MAX_CLIENTS 256
#define MAX_WINDOWS 1024
#define MAX_EVENTS 128
#define SOCKET_PATH "/tmp/.soiav-display"
#define SHM_PATH "/soiav-shm-"

// ==================== ТИПЫ ДАННЫХ ====================

typedef unsigned int color_t;  // ARGB

typedef struct {
    int x, y;
    int width, height;
} rect_t;

typedef struct {
    int type;      // 0=move, 1=resize, 2=fade, 3=scale
    int start_time;
    int duration;
    float start_val[4];
    float end_val[4];
    float current_val[4];
    int active;
} animation_t;

typedef struct {
    float opacity;
    float blur;
    float shadow;
    int rounded;
    color_t shadow_color;
} effects_t;

typedef struct window {
    int id;
    int client_id;
    char title[256];
    rect_t geom;
    rect_t saved_geom;
    color_t *fb;           // Framebuffer окна
    color_t *composited;    // Скомпозитированный буфер
    int depth;
    int flags;  // 1=visible, 2=focused, 4=minimized, 8=maximized, 16=fullscreen
    effects_t effects;
    animation_t anim;
    struct window *next;
    pthread_mutex_t lock;
} window_t;

typedef struct {
    int id;
    int socket;
    char name[64];
    pid_t pid;
    window_t *windows;
    int window_count;
    int auth;
    pthread_mutex_t lock;
} client_t;

typedef struct {
    int type;  // 0=key, 1=mouse, 2=window
    int window_id;
    int client_id;
    union {
        struct { int key, mod; } key;
        struct { int x, y, button, wheel; } mouse;
        struct { int x, y, w, h; } window;
    } data;
    unsigned long long time;
} event_t;

typedef struct {
    int fb_fd;
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    color_t *back_buffer;
    color_t *front_buffer;
    int width, height;
    int stride;
    int bpp;
} screen_t;

// ==================== ГЛОБАЛЬНЫЙ СЕРВЕР ====================

static struct {
    client_t clients[MAX_CLIENTS];
    int client_count;
    
    window_t *windows;
    int window_count;
    int next_window_id;
    
    screen_t screen;
    
    event_t events[MAX_EVENTS];
    int event_head;
    int event_tail;
    
    point_t mouse_pos;
    int mouse_buttons;
    
    int focused_window;
    int running;
    
    int server_socket;
    
    pthread_t compositor_thread;
    pthread_t input_thread;
    pthread_mutex_t global_lock;
    pthread_mutex_t event_lock;
} server;

// ==================== EASING FUNCTIONS ====================

static float ease_linear(float t) { return t; }
static float ease_in_quad(float t) { return t * t; }
static float ease_out_quad(float t) { return t * (2 - t); }
static float ease_in_out_quad(float t) { 
    return t < 0.5 ? 2 * t * t : -1 + (4 - 2 * t) * t; 
}
static float ease_out_back(float t) {
    float c1 = 1.70158;
    float c3 = c1 + 1;
    return 1 + c3 * pow(t - 1, 3) + c1 * pow(t - 1, 2);
}

// ==================== ГРАФИЧЕСКИЕ ПРИМИТИВЫ ====================

static color_t make_color(int r, int g, int b, int a) {
    return (a << 24) | (r << 16) | (g << 8) | b;
}

static int get_red(color_t c) { return (c >> 16) & 0xFF; }
static int get_green(color_t c) { return (c >> 8) & 0xFF; }
static int get_blue(color_t c) { return c & 0xFF; }
static int get_alpha(color_t c) { return (c >> 24) & 0xFF; }

static color_t blend_colors(color_t src, color_t dst) {
    int a_src = get_alpha(src);
    if (a_src == 0) return dst;
    if (a_src == 255) return src;
    
    int a_dst = get_alpha(dst);
    int a_out = a_src + a_dst * (255 - a_src) / 255;
    
    int r = (get_red(src) * a_src + get_red(dst) * a_dst * (255 - a_src) / 255) / a_out;
    int g = (get_green(src) * a_src + get_green(dst) * a_dst * (255 - a_src) / 255) / a_out;
    int b = (get_blue(src) * a_src + get_blue(dst) * a_dst * (255 - a_src) / 255) / a_out;
    
    return make_color(r, g, b, a_out);
}

static void draw_pixel(color_t *fb, int x, int y, int width, color_t color) {
    if (x >= 0 && x < width && y >= 0 && y < server.screen.height) {
        fb[y * width + x] = blend_colors(color, fb[y * width + x]);
    }
}

static void draw_rect(color_t *fb, int x, int y, int w, int h, int screen_w, color_t color) {
    for (int j = y; j < y + h && j < server.screen.height; j++) {
        if (j < 0) continue;
        for (int i = x; i < x + w && i < screen_w; i++) {
            if (i >= 0) {
                fb[j * screen_w + i] = blend_colors(color, fb[j * screen_w + i]);
            }
        }
    }
}

static void draw_rect_rounded(color_t *fb, int x, int y, int w, int h, int screen_w, 
                               int radius, color_t color) {
    if (radius <= 0) {
        draw_rect(fb, x, y, w, h, screen_w, color);
        return;
    }
    
    for (int j = y; j < y + h; j++) {
        if (j < 0 || j >= server.screen.height) continue;
        for (int i = x; i < x + w; i++) {
            if (i < 0 || i >= screen_w) continue;
            
            // Проверка скругления
            if (i < x + radius && j < y + radius) {
                // Верхний левый угол
                int dx = x + radius - i;
                int dy = y + radius - j;
                if (dx * dx + dy * dy > radius * radius) continue;
            } else if (i > x + w - radius && j < y + radius) {
                // Верхний правый угол
                int dx = i - (x + w - radius);
                int dy = y + radius - j;
                if (dx * dx + dy * dy > radius * radius) continue;
            } else if (i < x + radius && j > y + h - radius) {
                // Нижний левый угол
                int dx = x + radius - i;
                int dy = j - (y + h - radius);
                if (dx * dx + dy * dy > radius * radius) continue;
            } else if (i > x + w - radius && j > y + h - radius) {
                // Нижний правый угол
                int dx = i - (x + w - radius);
                int dy = j - (y + h - radius);
                if (dx * dx + dy * dy > radius * radius) continue;
            }
            
            fb[j * screen_w + i] = blend_colors(color, fb[j * screen_w + i]);
        }
    }
}

static void draw_shadow(color_t *fb, int x, int y, int w, int h, int screen_w,
                        int radius, color_t color) {
    for (int j = y - radius; j < y + h + radius; j++) {
        if (j < 0 || j >= server.screen.height) continue;
        for (int i = x - radius; i < x + w + radius; i++) {
            if (i < 0 || i >= screen_w) continue;
            
            // Расстояние до окна
            int dx, dy;
            if (i < x) dx = x - i;
            else if (i > x + w) dx = i - (x + w);
            else dx = 0;
            
            if (j < y) dy = y - j;
            else if (j > y + h) dy = j - (y + h);
            else dy = 0;
            
            int dist = (int)sqrt(dx*dx + dy*dy);
            if (dist < radius) {
                int alpha = (radius - dist) * get_alpha(color) / radius;
                color_t shadow = (color & 0x00FFFFFF) | (alpha << 24);
                fb[j * screen_w + i] = blend_colors(shadow, fb[j * screen_w + i]);
            }
        }
    }
}

// Простой Gaussian blur (3x3)
static void apply_blur(color_t *src, color_t *dst, int width, int height) {
    int kernel[3][3] = {
        {1, 2, 1},
        {2, 4, 2},
        {1, 2, 1}
    };
    int kernel_sum = 16;
    
    for (int y = 1; y < height - 1; y++) {
        for (int x = 1; x < width - 1; x++) {
            int r = 0, g = 0, b = 0;
            
            for (int ky = -1; ky <= 1; ky++) {
                for (int kx = -1; kx <= 1; kx++) {
                    color_t c = src[(y + ky) * width + (x + kx)];
                    int weight = kernel[ky+1][kx+1];
                    r += get_red(c) * weight;
                    g += get_green(c) * weight;
                    b += get_blue(c) * weight;
                }
            }
            
            dst[y * width + x] = make_color(r / kernel_sum, g / kernel_sum, 
                                            b / kernel_sum, 255);
        }
    }
}

// ==================== УПРАВЛЕНИЕ ОКНАМИ ====================

static window_t* create_window(int client_id, int x, int y, int w, int h, 
                                const char *title, int flags) {
    pthread_mutex_lock(&server.global_lock);
    
    window_t *win = calloc(1, sizeof(window_t));
    win->id = server.next_window_id++;
    win->client_id = client_id;
    strncpy(win->title, title, 255);
    win->geom.x = x;
    win->geom.y = y;
    win->geom.width = w;
    win->geom.height = h;
    win->saved_geom = win->geom;
    win->flags = flags | 1; // visible
    win->depth = 32;
    win->effects.opacity = 1.0;
    win->effects.shadow = 10;
    win->effects.rounded = 8;
    win->effects.shadow_color = make_color(0, 0, 0, 100);
    
    // Выделяем память для framebuffer
    win->fb = calloc(w * h, sizeof(color_t));
    win->composited = calloc(w * h, sizeof(color_t));
    
    pthread_mutex_init(&win->lock, NULL);
    
    // Добавляем в список окон (по Z-order)
    win->next = server.windows;
    server.windows = win;
    server.window_count++;
    
    pthread_mutex_unlock(&server.global_lock);
    
    return win;
}

static void destroy_window(int window_id) {
    pthread_mutex_lock(&server.global_lock);
    
    window_t **ptr = &server.windows;
    while (*ptr) {
        window_t *win = *ptr;
        if (win->id == window_id) {
            *ptr = win->next;
            free(win->fb);
            free(win->composited);
            pthread_mutex_destroy(&win->lock);
            free(win);
            server.window_count--;
            break;
        }
        ptr = &win->next;
    }
    
    if (server.focused_window == window_id) {
        server.focused_window = 0;
    }
    
    pthread_mutex_unlock(&server.global_lock);
}

static window_t* find_window(int window_id) {
    window_t *win = server.windows;
    while (win) {
        if (win->id == window_id) return win;
        win = win->next;
    }
    return NULL;
}

static void move_window(int window_id, int x, int y) {
    window_t *win = find_window(window_id);
    if (win) {
        pthread_mutex_lock(&win->lock);
        win->geom.x = x;
        win->geom.y = y;
        pthread_mutex_unlock(&win->lock);
    }
}

static void resize_window(int window_id, int w, int h) {
    window_t *win = find_window(window_id);
    if (win) {
        pthread_mutex_lock(&win->lock);
        win->geom.width = w;
        win->geom.height = h;
        // Перевыделяем framebuffer
        free(win->fb);
        free(win->composited);
        win->fb = calloc(w * h, sizeof(color_t));
        win->composited = calloc(w * h, sizeof(color_t));
        pthread_mutex_unlock(&win->lock);
    }
}

static void set_window_flags(int window_id, int flags, int set) {
    window_t *win = find_window(window_id);
    if (win) {
        pthread_mutex_lock(&win->lock);
        if (set) win->flags |= flags;
        else win->flags &= ~flags;
        
        // Обработка максимизации
        if ((flags & 8) && set) {
            win->saved_geom = win->geom;
            win->geom.x = 0;
            win->geom.y = 0;
            win->geom.width = server.screen.width;
            win->geom.height = server.screen.height - 50; // Оставляем место для панели задач
        } else if ((flags & 8) && !set) {
            win->geom = win->saved_geom;
        }
        
        pthread_mutex_unlock(&win->lock);
    }
}

// ==================== АНИМАЦИИ ====================

static void start_animation(window_t *win, int type, int duration, 
                             float *start, float *end) {
    win->anim.active = 1;
    win->anim.type = type;
    win->anim.start_time = get_time_ms();
    win->anim.duration = duration;
    memcpy(win->anim.start_val, start, 4 * sizeof(float));
    memcpy(win->anim.end_val, end, 4 * sizeof(float));
}

static void update_animations(void) {
    unsigned long long now = get_time_ms();
    window_t *win = server.windows;
    
    while (win) {
        if (win->anim.active) {
            float t = (float)(now - win->anim.start_time) / win->anim.duration;
            if (t >= 1.0) {
                win->anim.active = 0;
                memcpy(win->anim.current_val, win->anim.end_val, 4 * sizeof(float));
            } else {
                // Применяем easing
                float eased = ease_out_back(t);
                for (int i = 0; i < 4; i++) {
                    win->anim.current_val[i] = win->anim.start_val[i] + 
                        (win->anim.end_val[i] - win->anim.start_val[i]) * eased;
                }
                
                // Применяем анимацию к окну
                switch(win->anim.type) {
                    case 0: // move
                        win->geom.x = win->anim.current_val[0];
                        win->geom.y = win->anim.current_val[1];
                        break;
                    case 1: // resize
                        win->geom.width = win->anim.current_val[0];
                        win->geom.height = win->anim.current_val[1];
                        break;
                    case 2: // fade
                        win->effects.opacity = win->anim.current_val[0];
                        break;
                    case 3: // scale
                        // Масштабирование через матрицу трансформации
                        break;
                }
            }
        }
        win = win->next;
    }
}

// Анимация открытия окна
static void animate_window_open(window_t *win) {
    float start[4] = {0, 0, 0, 0};
    float end[4] = {1, 1, 1, 1};
    start_animation(win, 2, 300, start, end); // fade in
}

// Анимация закрытия окна
static void animate_window_close(window_t *win) {
    float start[4] = {1, 1, 1, 1};
    float end[4] = {0, 0, 0, 0};
    start_animation(win, 2, 300, start, end); // fade out
}

// Анимация перемещения
static void animate_window_move(window_t *win, int x, int y) {
    float start[4] = {win->geom.x, win->geom.y, 0, 0};
    float end[4] = {x, y, 0, 0};
    start_animation(win, 0, 250, start, end);
}

// ==================== КОМПОЗИТИНГ ====================

static void composite_frame(void) {
    if (!server.screen.back_buffer) return;
    
    // Очищаем back buffer
    memset(server.screen.back_buffer, 0, 
           server.screen.width * server.screen.height * sizeof(color_t));
    
    // Рисуем фон (градиент)
    for (int y = 0; y < server.screen.height; y++) {
        for (int x = 0; x < server.screen.width; x++) {
            color_t color = make_color(
                0x2D + (y * 10 / server.screen.height),
                0x2D + (y * 10 / server.screen.height),
                0x2D + (y * 10 / server.screen.height),
                255
            );
            server.screen.back_buffer[y * server.screen.width + x] = color;
        }
    }
    
    // Композитинг окон (сзади наперед)
    window_t *win = server.windows;
    while (win) {
        if (win->flags & 1) { // visible
            pthread_mutex_lock(&win->lock);
            
            // Тень
            if (win->effects.shadow > 0) {
                draw_shadow(server.screen.back_buffer,
                           win->geom.x, win->geom.y,
                           win->geom.width, win->geom.height,
                           server.screen.width,
                           win->effects.shadow,
                           win->effects.shadow_color);
            }
            
            // Скругленные углы
            draw_rect_rounded(server.screen.back_buffer,
                             win->geom.x, win->geom.y,
                             win->geom.width, win->geom.height,
                             server.screen.width,
                             win->effects.rounded,
                             make_color(0x3D, 0x3D, 0x3D, 255));
            
            // Заголовок окна
            draw_rect_rounded(server.screen.back_buffer,
                             win->geom.x, win->geom.y,
                             win->geom.width, 30,
                             server.screen.width,
                             win->effects.rounded,
                             make_color(0x1A, 0x1A, 0x1A, 255));
            
            // Текст заголовка (упрощенно)
            for (int i = 0; win->title[i] && i < 50; i++) {
                // Здесь должен быть рендеринг текста
            }
            
            // Кнопки окна
            draw_rect(server.screen.back_buffer,
                     win->geom.x + win->geom.width - 45, win->geom.y + 5,
                     10, 10, server.screen.width,
                     make_color(0xFF, 0x55, 0x55, 255)); // close
            draw_rect(server.screen.back_buffer,
                     win->geom.x + win->geom.width - 30, win->geom.y + 5,
                     10, 10, server.screen.width,
                     make_color(0x55, 0xFF, 0x55, 255)); // maximize
            draw_rect(server.screen.back_buffer,
                     win->geom.x + win->geom.width - 15, win->geom.y + 5,
                     10, 10, server.screen.width,
                     make_color(0xFF, 0xFF, 0x55, 255)); // minimize
            
            // Клиентская область
            draw_rect(server.screen.back_buffer,
                     win->geom.x, win->geom.y + 30,
                     win->geom.width, win->geom.height - 30,
                     server.screen.width,
                     make_color(0x2D, 0x2D, 0x2D, 255));
            
            // Контент окна из framebuffer
            if (win->fb) {
                for (int y = 0; y < win->geom.height - 30; y++) {
                    for (int x = 0; x < win->geom.width; x++) {
                        if (y * win->geom.width + x < win->geom.width * win->geom.height) {
                            color_t c = win->fb[y * win->geom.width + x];
                            int screen_y = win->geom.y + 30 + y;
                            int screen_x = win->geom.x + x;
                            
                            if (screen_x >= 0 && screen_x < server.screen.width &&
                                screen_y >= 0 && screen_y < server.screen.height) {
                                // Применяем прозрачность
                                int alpha = get_alpha(c) * win->effects.opacity;
                                c = (c & 0x00FFFFFF) | (alpha << 24);
                                
                                server.screen.back_buffer[screen_y * server.screen.width + screen_x] =
                                    blend_colors(c, server.screen.back_buffer[screen_y * server.screen.width + screen_x]);
                            }
                        }
                    }
                }
            }
            
            // Рамка фокуса
            if (win->id == server.focused_window) {
                draw_rect(server.screen.back_buffer,
                         win->geom.x - 2, win->geom.y - 2,
                         win->geom.width + 4, win->geom.height + 4,
                         server.screen.width,
                         make_color(0x00, 0x78, 0xD7, 255));
            }
            
            pthread_mutex_unlock(&win->lock);
        }
        win = win->next;
    }
    
    // Рисуем курсор мыши
    draw_rect(server.screen.back_buffer,
             server.mouse_pos.x, server.mouse_pos.y,
             16, 16, server.screen.width,
             make_color(0xFF, 0xFF, 0xFF, 255));
    draw_rect(server.screen.back_buffer,
             server.mouse_pos.x + 2, server.mouse_pos.y + 2,
             12, 12, server.screen.width,
             make_color(0x00, 0x00, 0x00, 255));
    
    // Swap buffers
    color_t *tmp = server.screen.front_buffer;
    server.screen.front_buffer = server.screen.back_buffer;
    server.screen.back_buffer = tmp;
    
    // Копируем в framebuffer устройства (упрощенно)
    if (server.screen.fb_fd >= 0) {
        lseek(server.screen.fb_fd, 0, SEEK_SET);
        write(server.screen.fb_fd, server.screen.front_buffer,
              server.screen.width * server.screen.height * sizeof(color_t));
    }
}

// ==================== УПРАВЛЕНИЕ СОБЫТИЯМИ ====================

static void push_event(event_t *ev) {
    pthread_mutex_lock(&server.event_lock);
    
    server.events[server.event_head] = *ev;
    server.event_head = (server.event_head + 1) % MAX_EVENTS;
    
    if (server.event_head == server.event_tail) {
        // Переполнение - теряем самое старое событие
        server.event_tail = (server.event_tail + 1) % MAX_EVENTS;
    }
    
    pthread_mutex_unlock(&server.event_lock);
}

static int pop_event(client_t *client, event_t *ev) {
    // Здесь должна быть доставка событий конкретному клиенту
    // Упрощенно - берем глобальное событие
    pthread_mutex_lock(&server.event_lock);
    
    if (server.event_tail != server.event_head) {
        *ev = server.events[server.event_tail];
        server.event_tail = (server.event_tail + 1) % MAX_EVENTS;
        pthread_mutex_unlock(&server.event_lock);
        return 1;
    }
    
    pthread_mutex_unlock(&server.event_lock);
    return 0;
}

// ==================== ОБРАБОТКА ВВОДА ====================

static void handle_keyboard_event(struct input_event *ev) {
    event_t event;
    event.type = (ev->value) ? EVENT_KEY_PRESS : EVENT_KEY_RELEASE;
    event.time = get_time_ms();
    event.key.key = ev->code;
    event.key.mod = 0; // TODO: modifier keys
    event.window_id = server.focused_window;
    
    push_event(&event);
}

static void handle_mouse_event(struct input_event *ev) {
    static int last_x = 0, last_y = 0;
    
    if (ev->type == EV_REL) {
        if (ev->code == REL_X) {
            server.mouse_pos.x += ev->value;
            if (server.mouse_pos.x < 0) server.mouse_pos.x = 0;
            if (server.mouse_pos.x >= server.screen.width) 
                server.mouse_pos.x = server.screen.width - 1;
                
            event_t event;
            event.type = EVENT_MOUSE_MOVE;
            event.time = get_time_ms();
            event.mouse.x = server.mouse_pos.x;
            event.mouse.y = server.mouse_pos.y;
            
            // Определяем окно под курсором
            window_t *win = server.windows;
            while (win) {
                if (win->flags & 1 &&
                    server.mouse_pos.x >= win->geom.x &&
                    server.mouse_pos.x <= win->geom.x + win->geom.width &&
                    server.mouse_pos.y >= win->geom.y &&
                    server.mouse_pos.y <= win->geom.y + win->geom.height) {
                    event.window_id = win->id;
                    break;
                }
                win = win->next;
            }
            
            push_event(&event);
        }
        if (ev->code == REL_Y) {
            server.mouse_pos.y += ev->value;
            if (server.mouse_pos.y < 0) server.mouse_pos.y = 0;
            if (server.mouse_pos.y >= server.screen.height) 
                server.mouse_pos.y = server.screen.height - 1;
        }
    } else if (ev->type == EV_KEY) {
        if (ev->code >= BTN_LEFT && ev->code <= BTN_MIDDLE) {
            int button = ev->code - BTN_LEFT + 1;
            
            if (ev->value) {
                server.mouse_buttons |= (1 << (button - 1));
                
                // Проверка на заголовок окна для перетаскивания
                window_t *win = server.windows;
                while (win) {
                    if (win->flags & 1 &&
                        server.mouse_pos.x >= win->geom.x &&
                        server.mouse_pos.x <= win->geom.x + win->geom.width &&
                        server.mouse_pos.y >= win->geom.y &&
                        server.mouse_pos.y <= win->geom.y + 30) {
                        // Клик в заголовок - фокус и возможность перетаскивания
                        server.focused_window = win->id;
                        last_x = server.mouse_pos.x;
                        last_y = server.mouse_pos.y;
                        break;
                    }
                    win = win->next;
                }
            } else {
                server.mouse_buttons &= ~(1 << (button - 1));
            }
            
            event_t event;
            event.type = ev->value ? EVENT_MOUSE_PRESS : EVENT_MOUSE_RELEASE;
            event.time = get_time_ms();
            event.mouse.x = server.mouse_pos.x;
            event.mouse.y = server.mouse_pos.y;
            event.mouse.button = button;
            event.window_id = server.focused_window;
            
            push_event(&event);
        }
    }
}

static void* input_thread_func(void *arg) {
    int fd = open("/dev/input/mice", O_RDONLY);
    if (fd < 0) {
        // Пробуем PS/2
        fd = open("/dev/psaux", O_RDONLY);
        if (fd < 0) {
            printf("DisplayServer: Не удалось открыть устройство мыши\n");
            return NULL;
        }
    }
    
    struct input_event ev;
    while (server.running) {
        if (read(fd, &ev, sizeof(ev)) == sizeof(ev)) {
            handle_mouse_event(&ev);
        }
        usleep(1000);
    }
    
    close(fd);
    return NULL;
}

// ==================== СЕТЕВОЙ ПРОТОКОЛ ====================

// Протокол Soiav Display Protocol (SDP)
enum {
    SDP_CREATE_WINDOW = 1,
    SDP_DESTROY_WINDOW,
    SDP_MOVE_WINDOW,
    SDP_RESIZE_WINDOW,
    SDP_SET_WINDOW_FLAGS,
    SDP_UPDATE_FRAME,
    SDP_GET_EVENT,
    SDP_SET_TITLE,
    SDP_GET_SCREEN_INFO,
    SDP_PING,
    SDP_CLOSE
};

typedef struct {
    int type;
    int window_id;
    int client_id;
    union {
        struct { int x, y, w, h; char title[256]; } create;
        struct { int x, y; } move;
        struct { int w, h; } resize;
        struct { int flags; int set; } flags;
        struct { int size; } frame;
        struct { char title[256]; } title;
        struct { int width, height, depth; } screen;
    } data;
} sdp_packet_t;

static void handle_client(client_t *client) {
    sdp_packet_t packet;
    
    while (server.running) {
        int n = read(client->socket, &packet, sizeof(packet));
        if (n <= 0) break;
        
        switch(packet.type) {
            case SDP_CREATE_WINDOW: {
                window_t *win = create_window(client->id,
                                             packet.data.create.x,
                                             packet.data.create.y,
                                             packet.data.create.w,
                                             packet.data.create.h,
                                             packet.data.create.title,
                                             1);
                if (win) {
                    // Отправляем ID окна клиенту
                    write(client->socket, &win->id, sizeof(int));
                    animate_window_open(win);
                }
                break;
            }
            
            case SDP_DESTROY_WINDOW: {
                animate_window_close(find_window(packet.window_id));
                destroy_window(packet.window_id);
                break;
            }
            
            case SDP_MOVE_WINDOW: {
                window_t *win = find_window(packet.window_id);
                if (win && win->client_id == client->id) {
                    animate_window_move(win, packet.data.move.x, packet.data.move.y);
                }
                break;
            }
            
            case SDP_RESIZE_WINDOW: {
                window_t *win = find_window(packet.window_id);
                if (win && win->client_id == client->id) {
                    resize_window(packet.window_id, 
                                 packet.data.resize.w, 
                                 packet.data.resize.h);
                }
                break;
            }
            
            case SDP_SET_WINDOW_FLAGS: {
                window_t *win = find_window(packet.window_id);
                if (win && win->client_id == client->id) {
                    set_window_flags(packet.window_id,
                                    packet.data.flags.flags,
                                    packet.data.flags.set);
                }
                break;
            }
            
            case SDP_UPDATE_FRAME: {
                window_t *win = find_window(packet.window_id);
                if (win && win->client_id == client->id) {
                    // Читаем framebuffer из разделяемой памяти
                    // Упрощенно - копируем из сокета
                    pthread_mutex_lock(&win->lock);
                    read(client->socket, win->fb, 
                         win->geom.width * win->geom.height * sizeof(color_t));
                    pthread_mutex_unlock(&win->lock);
                }
                break;
            }
            
            case SDP_GET_EVENT: {
                event_t ev;
                if (pop_event(client, &ev)) {
                    write(client->socket, &ev, sizeof(ev));
                } else {
                    int zero = 0;
                    write(client->socket, &zero, sizeof(int));
                }
                break;
            }
            
            case SDP_GET_SCREEN_INFO: {
                sdp_packet_t resp;
                resp.type = SDP_GET_SCREEN_INFO;
                resp.data.screen.width = server.screen.width;
                resp.data.screen.height = server.screen.height;
                resp.data.screen.depth = 32;
                write(client->socket, &resp, sizeof(resp));
                break;
            }
            
            case SDP_PING: {
                write(client->socket, &packet, sizeof(packet));
                break;
            }
            
            case SDP_CLOSE:
                goto disconnect;
        }
    }
    
disconnect:
    printf("DisplayServer: Клиент %d отключился\n", client->id);
    close(client->socket);
    client->socket = -1;
}

static void* client_thread_func(void *arg) {
    client_t *client = (client_t*)arg;
    handle_client(client);
    return NULL;
}

static void* server_thread_func(void *arg) {
    struct sockaddr_un addr;
    
    server.server_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server.server_socket < 0) {
        printf("DisplayServer: Ошибка создания сокета\n");
        return NULL;
    }
    
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCKET_PATH);
    
    unlink(SOCKET_PATH);
    if (bind(server.server_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        printf("DisplayServer: Ошибка bind\n");
        close(server.server_socket);
        return NULL;
    }
    
    if (listen(server.server_socket, 5) < 0) {
        printf("DisplayServer: Ошибка listen\n");
        close(server.server_socket);
        return NULL;
    }
    
    printf("DisplayServer: Слушаем на %s\n", SOCKET_PATH);
    
    while (server.running) {
        int client_fd = accept(server.server_socket, NULL, NULL);
        if (client_fd < 0) continue;
        
        pthread_mutex_lock(&server.global_lock);
        
        if (server.client_count >= MAX_CLIENTS) {
            close(client_fd);
            pthread_mutex_unlock(&server.global_lock);
            continue;
        }
        
        client_t *client = &server.clients[server.client_count++];
        memset(client, 0, sizeof(client_t));
        client->id = server.client_count;
        client->socket = client_fd;
        client->pid = 0; // TODO: получить PID
        sprintf(client->name, "client-%d", client->id);
        pthread_mutex_init(&client->lock, NULL);
        
        pthread_t thread;
        pthread_create(&thread, NULL, client_thread_func, client);
        pthread_detach(thread);
        
        printf("DisplayServer: Новый клиент %d\n", client->id);
        
        pthread_mutex_unlock(&server.global_lock);
    }
    
    return NULL;
}

// ==================== КОМПОЗИТОР ====================

static void* compositor_thread_func(void *arg) {
    unsigned long long last_frame = 0;
    int frame_time = 1000 / 60; // 60 FPS
    
    while (server.running) {
        unsigned long long now = get_time_ms();
        
        if (now - last_frame >= frame_time) {
            update_animations();
            composite_frame();
            last_frame = now;
        }
        
        usleep(1000);
    }
    
    return NULL;
}

// ==================== ИНИЦИАЛИЗАЦИЯ ====================

static int init_framebuffer(void) {
    server.screen.fb_fd = open("/dev/fb0", O_RDWR);
    if (server.screen.fb_fd < 0) {
        // Эмулируем framebuffer в памяти
        server.screen.width = 1920;
        server.screen.height = 1080;
        server.screen.stride = server.screen.width;
        server.screen.bpp = 32;
    } else {
        // Получаем информацию о реальном framebuffer
        ioctl(server.screen.fb_fd, FBIOGET_VSCREENINFO, &server.screen.vinfo);
        ioctl(server.screen.fb_fd, FBIOGET_FSCREENINFO, &server.screen.finfo);
        
        server.screen.width = server.screen.vinfo.xres;
        server.screen.height = server.screen.vinfo.yres;
        server.screen.stride = server.screen.finfo.line_length / (server.screen.vinfo.bits_per_pixel / 8);
        server.screen.bpp = server.screen.vinfo.bits_per_pixel;
    }
    
    // Выделяем буферы
    int buffer_size = server.screen.width * server.screen.height * sizeof(color_t);
    server.screen.back_buffer = malloc(buffer_size);
    server.screen.front_buffer = malloc(buffer_size);
    
    if (!server.screen.back_buffer || !server.screen.front_buffer) {
        printf("DisplayServer: Ошибка выделения памяти для буферов\n");
        return -1;
    }
    
    printf("DisplayServer: Экран %dx%d (%d bpp)\n",
           server.screen.width, server.screen.height, server.screen.bpp);
    
    return 0;
}

int main(int argc, char **argv) {
    printf("Soiav Display Server v%s\n", SOIAV_DISPLAY_VERSION);
    
    // Инициализация
    memset(&server, 0, sizeof(server));
    server.running = 1;
    server.mouse_pos.x = server.screen.width / 2;
    server.mouse_pos.y = server.screen.height / 2;
    
    pthread_mutex_init(&server.global_lock, NULL);
    pthread_mutex_init(&server.event_lock, NULL);
    
    if (init_framebuffer() < 0) {
        return 1;
    }
    
    // Создаем тестовые окна для демонстрации
    create_window(0, 100, 100, 400, 300, "Калькулятор", 1);
    create_window(0, 200, 150, 500, 400, "Файловый менеджер", 1);
    create_window(0, 300, 200, 300, 200, "Терминал", 1);
    
    // Запускаем потоки
    pthread_create(&server.compositor_thread, NULL, compositor_thread_func, NULL);
    pthread_create(&server.input_thread, NULL, input_thread_func, NULL);
    
    // Запускаем сервер
    server_thread_func(NULL);
    
    // Ожидаем завершения
    pthread_join(server.compositor_thread, NULL);
    pthread_join(server.input_thread, NULL);
    
    // Очистка
    close(server.server_socket);
    unlink(SOCKET_PATH);
    
    if (server.screen.fb_fd >= 0) {
        close(server.screen.fb_fd);
    }
    
    free(server.screen.back_buffer);
    free(server.screen.front_buffer);
    
    printf("DisplayServer: Завершен\n");
    return 0;
}
