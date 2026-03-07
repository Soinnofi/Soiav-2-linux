// system/apps/calculator.c - Калькулятор (GUI приложение)
#include "../../userland/libc/soiav_libc.h"

// Стили окна
#define WINDOW_WIDTH 300
#define WINDOW_HEIGHT 400
#define BUTTON_WIDTH 60
#define BUTTON_HEIGHT 50
#define BUTTON_MARGIN 10

// Цвета
#define COLOR_BG 0x2D2D2D
#define COLOR_BUTTON 0x3D3D3D
#define COLOR_BUTTON_HOVER 0x4D4D4D
#define COLOR_OPERATOR 0x0078D7
#define COLOR_EQUALS 0x107C10
#define COLOR_TEXT 0xFFFFFF
#define COLOR_DISPLAY 0x1A1A1A

typedef struct {
    char display[64];
    double current_value;
    double memory;
    char operator;
    int new_number;
    int window_id;
} calculator_t;

static calculator_t calc;

// Обработчик кнопок
static void button_click(int x, int y) {
    // Определяем, какая кнопка нажата
    int col = (x - BUTTON_MARGIN) / (BUTTON_WIDTH + BUTTON_MARGIN);
    int row = (y - 100) / (BUTTON_HEIGHT + BUTTON_MARGIN);
    
    if (col < 0 || col > 3 || row < 0 || row > 4) return;
    
    // Матрица кнопок
    const char *buttons[5][4] = {
        {"7", "8", "9", "/"},
        {"4", "5", "6", "*"},
        {"1", "2", "3", "-"},
        {"0", ".", "=", "+"},
        {"C", "M+", "MR", "MC"}
    };
    
    const char *btn = buttons[row][col];
    
    if (strcmp(btn, "C") == 0) {
        // Очистка
        calc.display[0] = '0';
        calc.display[1] = '\0';
        calc.current_value = 0;
        calc.memory = 0;
        calc.operator = 0;
        calc.new_number = 1;
    } else if (strcmp(btn, "=") == 0) {
        // Вычисление
        double result = calc.current_value;
        double second = atof(calc.display);
        
        switch(calc.operator) {
            case '+': result = calc.current_value + second; break;
            case '-': result = calc.current_value - second; break;
            case '*': result = calc.current_value * second; break;
            case '/': 
                if (second != 0) 
                    result = calc.current_value / second; 
                else
                    strcpy(calc.display, "Ошибка");
                break;
            default: result = second; break;
        }
        
        if (strcmp(calc.display, "Ошибка") != 0) {
            sprintf(calc.display, "%g", result);
            calc.current_value = result;
            calc.operator = 0;
            calc.new_number = 1;
        }
    } else if (strcmp(btn, "M+") == 0) {
        // Добавить в память
        calc.memory += atof(calc.display);
        calc.new_number = 1;
    } else if (strcmp(btn, "MR") == 0) {
        // Вызвать из памяти
        sprintf(calc.display, "%g", calc.memory);
        calc.new_number = 0;
    } else if (strcmp(btn, "MC") == 0) {
        // Очистить память
        calc.memory = 0;
    } else if (strchr("+-/*", btn[0])) {
        // Оператор
        if (calc.operator && !calc.new_number) {
            // Вычисляем предыдущую операцию
            double second = atof(calc.display);
            switch(calc.operator) {
                case '+': calc.current_value += second; break;
                case '-': calc.current_value -= second; break;
                case '*': calc.current_value *= second; break;
                case '/': calc.current_value /= second; break;
            }
            sprintf(calc.display, "%g", calc.current_value);
        } else {
            calc.current_value = atof(calc.display);
        }
        calc.operator = btn[0];
        calc.new_number = 1;
    } else {
        // Цифра или точка
        if (calc.new_number) {
            calc.display[0] = btn[0];
            calc.display[1] = '\0';
            calc.new_number = 0;
        } else {
            strcat(calc.display, btn);
        }
    }
    
    // Обновляем дисплей
    soiav_draw_rect(calc.window_id, 10, 10, WINDOW_WIDTH - 20, 60, COLOR_DISPLAY);
    soiav_draw_text(calc.window_id, 20, 30, calc.display, COLOR_TEXT);
}

int main(void) {
    // Создание окна
    calc.window_id = soiav_create_window(WINDOW_WIDTH, WINDOW_HEIGHT, 
                                         0, "Калькулятор");
    if (calc.window_id < 0) {
        printf("Ошибка создания окна\n");
        return 1;
    }
    
    // Инициализация калькулятора
    strcpy(calc.display, "0");
    calc.current_value = 0;
    calc.memory = 0;
    calc.operator = 0;
    calc.new_number = 1;
    
    // Рисуем фон
    soiav_draw_rect(calc.window_id, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, COLOR_BG);
    
    // Рисуем дисплей
    soiav_draw_rect(calc.window_id, 10, 10, WINDOW_WIDTH - 20, 60, COLOR_DISPLAY);
    soiav_draw_text(calc.window_id, 20, 30, calc.display, COLOR_TEXT);
    
    // Рисуем кнопки
    const char *buttons[5][4] = {
        {"7", "8", "9", "/"},
        {"4", "5", "6", "*"},
        {"1", "2", "3", "-"},
        {"0", ".", "=", "+"},
        {"C", "M+", "MR", "MC"}
    };
    
    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 4; col++) {
            int x = BUTTON_MARGIN + col * (BUTTON_WIDTH + BUTTON_MARGIN);
            int y = 100 + row * (BUTTON_HEIGHT + BUTTON_MARGIN);
            
            // Цвет кнопки
            unsigned int color = COLOR_BUTTON;
            if (strchr("+-/*", buttons[row][col][0]))
                color = COLOR_OPERATOR;
            else if (strcmp(buttons[row][col], "=") == 0)
                color = COLOR_EQUALS;
            
            soiav_draw_rect(calc.window_id, x, y, BUTTON_WIDTH, BUTTON_HEIGHT, color);
            soiav_draw_text(calc.window_id, x + BUTTON_WIDTH/2 - 8, 
                           y + BUTTON_HEIGHT/2 - 8, 
                           buttons[row][col], COLOR_TEXT);
        }
    }
    
    // Главный цикл обработки событий
    while (1) {
        struct soiav_event event;
        
        if (soiav_get_input(calc.window_id, &event) > 0) {
            if (event.type == SOIAV_EVENT_MOUSE && event.button == 1) {
                // Левая кнопка мыши нажата
                button_click(event.x, event.y);
            } else if (event.type == SOIAV_EVENT_KEY) {
                // Клавиатурный ввод
                if (event.key >= '0' && event.key <= '9') {
                    char digit[2] = {event.key, 0};
                    if (calc.new_number) {
                        calc.display[0] = event.key;
                        calc.display[1] = '\0';
                        calc.new_number = 0;
                    } else {
                        strcat(calc.display, digit);
                    }
                    soiav_draw_rect(calc.window_id, 10, 10, 
                                   WINDOW_WIDTH - 20, 60, COLOR_DISPLAY);
                    soiav_draw_text(calc.window_id, 20, 30, 
                                   calc.display, COLOR_TEXT);
                }
            } else if (event.type == SOIAV_EVENT_CLOSE) {
                break;
            }
        }
        
        // Небольшая задержка
        soiav_usleep(10000);
    }
    
    return 0;
}
// system/apps/filemanager.c - Файловый менеджер
#include "../../userland/libc/soiav_libc.h"

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600
#define LIST_WIDTH 200
#define FILE_HEIGHT 24
#define ICON_SIZE 16

typedef struct {
    char name[256];
    int type; // 0=файл, 1=директория, 2=диск
    off_t size;
    time_t mtime;
} file_entry_t;

static file_entry_t current_files[1000];
static int file_count = 0;
static char current_path[256] = "/";
static int selected = -1;
static int scroll = 0;

// Получение списка файлов
static int list_directory(const char *path) {
    file_count = 0;
    
    void *dir = soiav_opendir(path);
    if (!dir) return -1;
    
    struct soiav_dirent *entry;
    while ((entry = soiav_readdir(dir)) && file_count < 1000) {
        strcpy(current_files[file_count].name, entry->name);
        current_files[file_count].type = entry->type;
        
        // Получаем размер и время для файлов
        if (entry->type == 0) {
            char fullpath[512];
            sprintf(fullpath, "%s/%s", path, entry->name);
            
            struct soiav_stat st;
            if (soiav_stat(fullpath, &st) == 0) {
                current_files[file_count].size = st.st_size;
                current_files[file_count].mtime = st.st_mtime;
            }
        }
        
        file_count++;
    }
    
    soiav_closedir(dir);
    
    // Сортировка: директории первые
    for (int i = 0; i < file_count - 1; i++) {
        for (int j = i + 1; j < file_count; j++) {
            int i_is_dir = (current_files[i].type == 1);
            int j_is_dir = (current_files[j].type == 1);
            
            if (j_is_dir && !i_is_dir) {
                file_entry_t tmp = current_files[i];
                current_files[i] = current_files[j];
                current_files[j] = tmp;
            }
        }
    }
    
    return 0;
}

// Отрисовка файлового менеджера
static void draw_filemanager(int window_id) {
    // Фон
    soiav_draw_rect(window_id, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0x2D2D2D);
    
    // Адресная строка
    soiav_draw_rect(window_id, 10, 10, WINDOW_WIDTH - 20, 40, 0x1A1A1A);
    soiav_draw_text(window_id, 20, 22, current_path, 0xFFFFFF);
    
    // Список файлов
    int y = 70 - scroll * FILE_HEIGHT;
    for (int i = 0; i < file_count; i++) {
        if (y + FILE_HEIGHT > 70 && y < WINDOW_HEIGHT - 30) {
            // Цвет фона для выделенного элемента
            if (i == selected) {
                soiav_draw_rect(window_id, 10, y, WINDOW_WIDTH - 20, 
                               FILE_HEIGHT, 0x0078D7);
            }
            
            // Иконка
            unsigned int icon_color;
            if (current_files[i].type == 1) {
                icon_color = 0xFFD700; // Желтый для папок
                soiav_draw_text(window_id, 20, y + 4, "📁", icon_color);
            } else {
                icon_color = 0xCCCCCC; // Серый для файлов
                soiav_draw_text(window_id, 20, y + 4, "📄", icon_color);
            }
            
            // Имя файла
            soiav_draw_text(window_id, 45, y + 4, current_files[i].name, 0xFFFFFF);
            
            // Размер
            if (current_files[i].type == 0) {
                char size_str[32];
                if (current_files[i].size < 1024)
                    sprintf(size_str, "%ld B", current_files[i].size);
                else if (current_files[i].size < 1024*1024)
                    sprintf(size_str, "%ld KB", current_files[i].size / 1024);
                else
                    sprintf(size_str, "%ld MB", current_files[i].size / (1024*1024));
                
                soiav_draw_text(window_id, WINDOW_WIDTH - 150, y + 4, 
                               size_str, 0x888888);
            }
            
            // Дата
            struct tm *tm = localtime(&current_files[i].mtime);
            char date_str[32];
            sprintf(date_str, "%02d:%02d %02d.%02d", 
                    tm->tm_hour, tm->tm_min, tm->tm_mday, tm->tm_mon + 1);
            soiav_draw_text(window_id, WINDOW_WIDTH - 300, y + 4, 
                           date_str, 0x888888);
        }
        y += FILE_HEIGHT;
    }
    
    // Статус бар
    soiav_draw_rect(window_id, 0, WINDOW_HEIGHT - 20, WINDOW_WIDTH, 20, 0x1A1A1A);
    char status[64];
    sprintf(status, "Файлов: %d", file_count);
    soiav_draw_text(window_id, 10, WINDOW_HEIGHT - 16, status, 0x888888);
}

int main(void) {
    // Создание окна
    int window_id = soiav_create_window(WINDOW_WIDTH, WINDOW_HEIGHT, 
                                        SOIAV_WINDOW_RESIZABLE, 
                                        "Файловый менеджер");
    if (window_id < 0) return 1;
    
    // Загружаем список файлов
    list_directory(current_path);
    draw_filemanager(window_id);
    
    // Главный цикл
    while (1) {
        struct soiav_event event;
        
        if (soiav_get_input(window_id, &event) > 0) {
            if (event.type == SOIAV_EVENT_MOUSE) {
                int y = event.y - 70 + scroll * FILE_HEIGHT;
                int file_index = y / FILE_HEIGHT;
                
                if (event.button == 1) { // ЛКМ
                    if (file_index >= 0 && file_index < file_count) {
                        selected = file_index;
                        
                        // Двойной клик - открыть
                        static int last_click = 0;
                        static int last_index = -1;
                        int current_time = time(NULL);
                        
                        if (last_index == file_index && 
                            current_time - last_click < 1) {
                            // Открываем директорию
                            if (current_files[file_index].type == 1) {
                                if (strcmp(current_files[file_index].name, "..") == 0) {
                                    // Вверх
                                    char *last_slash = strrchr(current_path, '/');
                                    if (last_slash > current_path)
                                        *last_slash = '\0';
                                    else
                                        strcpy(current_path, "/");
                                } else {
                                    char new_path[512];
                                    sprintf(new_path, "%s/%s", current_path,
                                            current_files[file_index].name);
                                    strcpy(current_path, new_path);
                                }
                                list_directory(current_path);
                                selected = -1;
                                scroll = 0;
                            }
                        }
                        
                        last_click = current_time;
                        last_index = file_index;
                    }
                    
                    draw_filemanager(window_id);
                } else if (event.wheel) {
                    // Скролл
                    scroll -= event.wheel;
                    if (scroll < 0) scroll = 0;
                    if (scroll * FILE_HEIGHT > file_count * FILE_HEIGHT - 500)
                        scroll = (file_count * FILE_HEIGHT - 500) / FILE_HEIGHT;
                    draw_filemanager(window_id);
                }
            } else if (event.type == SOIAV_EVENT_KEY) {
                if (event.key == SOIAV_KEY_UP) {
                    if (selected > 0) selected--;
                    draw_filemanager(window_id);
                } else if (event.key == SOIAV_KEY_DOWN) {
                    if (selected < file_count - 1) selected++;
                    draw_filemanager(window_id);
                } else if (event.key == SOIAV_KEY_ENTER && selected >= 0) {
                    if (current_files[selected].type == 1) {
                        if (strcmp(current_files[selected].name, "..") == 0) {
                            char *last_slash = strrchr(current_path, '/');
                            if (last_slash > current_path)
                                *last_slash = '\0';
                            else
                                strcpy(current_path, "/");
                        } else {
                            char new_path[512];
                            sprintf(new_path, "%s/%s", current_path,
                                    current_files[selected].name);
                            strcpy(current_path, new_path);
                        }
                        list_directory(current_path);
                        selected = -1;
                        scroll = 0;
                        draw_filemanager(window_id);
                    }
                }
            } else if (event.type == SOIAV_EVENT_CLOSE) {
                break;
            }
        }
        
        soiav_usleep(10000);
    }
    
    return 0;
}
