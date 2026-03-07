// userland/shell/shell.c - Soiav Shell
#include "../libc/soiav_libc.h"

#define MAX_CMD_LEN 1024
#define MAX_ARGS 64
#define MAX_HISTORY 100
#define PATH_MAX 256

// История команд
static char history[MAX_HISTORY][MAX_CMD_LEN];
static int history_count = 0;
static int history_pos = 0;

// Текущая директория
static char cwd[PATH_MAX];

// Цвета
#define COLOR_RESET "\033[0m"
#define COLOR_RED "\033[31m"
#define COLOR_GREEN "\033[32m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_BLUE "\033[34m"
#define COLOR_CYAN "\033[36m"
#define COLOR_WHITE "\033[37m"

// Приглашение ввода
static void print_prompt(void) {
    char hostname[64] = "soiav";
    char username[64] = "user";
    
    // Получаем имя пользователя
    char *user = getenv("USER");
    if (user) strcpy(username, user);
    
    // Получаем hostname
    soiav_gethostname(hostname, sizeof(hostname));
    
    // Получаем текущую директорию
    soiav_getcwd(cwd, sizeof(cwd));
    
    // Сокращаем home до ~
    char *home = getenv("HOME");
    if (home && strncmp(cwd, home, strlen(home)) == 0) {
        char tmp[PATH_MAX];
        tmp[0] = '~';
        strcpy(tmp + 1, cwd + strlen(home));
        strcpy(cwd, tmp);
    }
    
    printf(COLOR_GREEN "%s@%s" COLOR_RESET ":" COLOR_BLUE "%s" COLOR_RESET "$ ",
           username, hostname, cwd);
    fflush(stdout);
}

// Разбор командной строки
static int parse_command(char *cmd, char **args) {
    int argc = 0;
    char *p = cmd;
    
    while (*p && argc < MAX_ARGS - 1) {
        // Пропускаем пробелы
        while (*p == ' ') p++;
        if (!*p) break;
        
        args[argc++] = p;
        
        // Ищем конец аргумента
        while (*p && *p != ' ') p++;
        if (*p) {
            *p = '\0';
            p++;
        }
    }
    
    args[argc] = NULL;
    return argc;
}

// Добавление команды в историю
static void add_to_history(const char *cmd) {
    if (cmd[0] == '\0') return;
    
    // Не добавляем дубликаты с последней командой
    if (history_count > 0 && strcmp(history[history_count - 1], cmd) == 0)
        return;
    
    strcpy(history[history_count % MAX_HISTORY], cmd);
    history_count++;
    history_pos = history_count;
}

// Поиск в PATH
static char *find_in_path(const char *cmd) {
    static char fullpath[PATH_MAX];
    char *path = getenv("PATH");
    
    if (!path) return NULL;
    
    // Если команда содержит /
    if (strchr(cmd, '/')) {
        if (soiav_access(cmd, X_OK) == 0)
            return (char*)cmd;
        return NULL;
    }
    
    char *p = path;
    char *end;
    
    while (*p) {
        end = strchr(p, ':');
        if (!end) end = p + strlen(p);
        
        int len = end - p;
        strncpy(fullpath, p, len);
        fullpath[len] = '/';
        strcpy(fullpath + len + 1, cmd);
        
        if (soiav_access(fullpath, X_OK) == 0)
            return fullpath;
        
        p = end;
        if (*p == ':') p++;
    }
    
    return NULL;
}

// ==================== ВСТРОЕННЫЕ КОМАНДЫ ====================

static int cmd_cd(int argc, char **argv) {
    char *path = (argc > 1) ? argv[1] : getenv("HOME");
    
    if (soiav_chdir(path) < 0) {
        printf("cd: %s: %s\n", path, "Нет такой директории");
        return 1;
    }
    
    return 0;
}

static int cmd_pwd(int argc, char **argv) {
    char cwd[PATH_MAX];
    if (soiav_getcwd(cwd, sizeof(cwd))) {
        printf("%s\n", cwd);
    }
    return 0;
}

static int cmd_ls(int argc, char **argv) {
    char *path = (argc > 1) ? argv[1] : ".";
    
    void *dir = soiav_opendir(path);
    if (!dir) {
        printf("ls: %s: %s\n", path, "Нет доступа");
        return 1;
    }
    
    struct soiav_dirent *entry;
    while ((entry = soiav_readdir(dir))) {
        if (entry->name[0] == '.') continue; // Пропускаем скрытые
        
        // Определяем тип
        if (entry->type == DT_DIR)
            printf(COLOR_BLUE "%s" COLOR_RESET "/  ", entry->name);
        else if (entry->type == DT_EXEC)
            printf(COLOR_GREEN "%s" COLOR_RESET "*  ", entry->name);
        else
            printf("%s  ", entry->name);
    }
    printf("\n");
    
    soiav_closedir(dir);
    return 0;
}

static int cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        printf("%s", argv[i]);
        if (i < argc - 1) printf(" ");
    }
    printf("\n");
    return 0;
}

static int cmd_clear(int argc, char **argv) {
    printf("\033[2J\033[H"); // ANSI escape для очистки
    return 0;
}

static int cmd_help(int argc, char **argv) {
    printf("Soiav Shell v1.0 - Встроенные команды:\n");
    printf("  cd [dir]     - Сменить директорию\n");
    printf("  pwd          - Показать текущую директорию\n");
    printf("  ls [dir]     - Показать содержимое директории\n");
    printf("  echo [text]  - Вывести текст\n");
    printf("  clear        - Очистить экран\n");
    printf("  exit         - Выйти из оболочки\n");
    printf("  help         - Показать эту справку\n");
    printf("  history      - Показать историю команд\n");
    printf("  ps           - Показать процессы\n");
    printf("  kill <pid>   - Завершить процесс\n");
    printf("  mkdir <dir>  - Создать директорию\n");
    printf("  rm <file>    - Удалить файл\n");
    printf("  cat <file>   - Показать содержимое файла\n");
    return 0;
}

static int cmd_history(int argc, char **argv) {
    int start = (history_count > MAX_HISTORY) ? 
                history_count - MAX_HISTORY : 0;
    
    for (int i = start; i < history_count; i++) {
        printf("%5d  %s\n", i + 1, history[i % MAX_HISTORY]);
    }
    return 0;
}

static int cmd_ps(int argc, char **argv) {
    printf("PID\tTTY\tSTAT\tTIME\tCOMMAND\n");
    
    void *dir = soiav_opendir("/proc");
    if (!dir) return 1;
    
    struct soiav_dirent *entry;
    while ((entry = soiav_readdir(dir))) {
        if (entry->type == DT_DIR && atoi(entry->name) > 0) {
            int pid = atoi(entry->name);
            char path[64];
            char cmdline[256];
            
            sprintf(path, "/proc/%d/cmdline", pid);
            FILE *f = fopen(path, "r");
            if (f) {
                fread(cmdline, 1, sizeof(cmdline), f);
                fclose(f);
                
                // Заменяем нули пробелами
                for (int i = 0; i < sizeof(cmdline); i++)
                    if (cmdline[i] == 0) cmdline[i] = ' ';
                
                printf("%d\t?\tR\t0:00\t%s\n", pid, cmdline);
            }
        }
    }
    
    soiav_closedir(dir);
    return 0;
}

static int cmd_kill(int argc, char **argv) {
    if (argc < 2) {
        printf("kill: требуется PID\n");
        return 1;
    }
    
    int pid = atoi(argv[1]);
    if (soiav_kill(pid, 9) < 0) {
        printf("kill: не удалось завершить процесс %d\n", pid);
        return 1;
    }
    
    return 0;
}

static int cmd_mkdir(int argc, char **argv) {
    if (argc < 2) {
        printf("mkdir: требуется имя директории\n");
        return 1;
    }
    
    if (soiav_mkdir(argv[1], 0755) < 0) {
        printf("mkdir: не удалось создать %s\n", argv[1]);
        return 1;
    }
    
    return 0;
}

static int cmd_rm(int argc, char **argv) {
    if (argc < 2) {
        printf("rm: требуется имя файла\n");
        return 1;
    }
    
    if (soiav_unlink(argv[1]) < 0) {
        printf("rm: не удалось удалить %s\n", argv[1]);
        return 1;
    }
    
    return 0;
}

static int cmd_cat(int argc, char **argv) {
    if (argc < 2) {
        printf("cat: требуется имя файла\n");
        return 1;
    }
    
    FILE *f = fopen(argv[1], "r");
    if (!f) {
        printf("cat: %s: %s\n", argv[1], "Нет такого файла");
        return 1;
    }
    
    char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        fwrite(buffer, 1, n, stdout);
    }
    
    fclose(f);
    return 0;
}

// Таблица встроенных команд
typedef struct {
    const char *name;
    int (*func)(int, char**);
    const char *help;
} builtin_t;

static builtin_t builtins[] = {
    {"cd", cmd_cd, "Сменить директорию"},
    {"pwd", cmd_pwd, "Показать текущую директорию"},
    {"ls", cmd_ls, "Показать содержимое директории"},
    {"echo", cmd_echo, "Вывести текст"},
    {"clear", cmd_clear, "Очистить экран"},
    {"exit", NULL, "Выйти из оболочки"},
    {"help", cmd_help, "Показать справку"},
    {"history", cmd_history, "Показать историю"},
    {"ps", cmd_ps, "Показать процессы"},
    {"kill", cmd_kill, "Завершить процесс"},
    {"mkdir", cmd_mkdir, "Создать директорию"},
    {"rm", cmd_rm, "Удалить файл"},
    {"cat", cmd_cat, "Показать файл"},
    {NULL, NULL, NULL}
};

// Выполнение команды
static int execute_command(char *cmd) {
    char *args[MAX_ARGS];
    int argc = parse_command(cmd, args);
    
    if (argc == 0) return 0;
    
    // Проверка встроенных команд
    for (int i = 0; builtins[i].name; i++) {
        if (strcmp(args[0], builtins[i].name) == 0) {
            if (builtins[i].func)
                return builtins[i].func(argc, args);
            else
                return -1; // Специальная обработка
        }
    }
    
    // Поиск исполняемого файла
    char *fullpath = find_in_path(args[0]);
    if (!fullpath) {
        printf("%s: команда не найдена\n", args[0]);
        return 1;
    }
    
    // Запуск программы
    int pid = soiav_fork();
    if (pid == 0) {
        // Дочерний процесс
        soiav_execve(fullpath, args, NULL);
        printf("Ошибка запуска %s\n", args[0]);
        soiav_exit(1);
    } else if (pid > 0) {
        // Родитель ждет завершения
        int status;
        soiav_waitpid(pid, &status, 0);
        return soiav_wexitstatus(status);
    } else {
        printf("Ошибка создания процесса\n");
        return 1;
    }
}

// Обработка стрелок для истории
static void handle_history(int key, char *buffer, int *pos) {
    if (key == 0x48) { // Стрелка вверх
        if (history_pos > 0) {
            history_pos--;
            // Очищаем текущую строку
            while (*pos > 0) {
                printf("\b \b");
                (*pos)--;
            }
            // Вставляем команду из истории
            strcpy(buffer, history[history_pos % MAX_HISTORY]);
            *pos = strlen(buffer);
            printf("%s", buffer);
        }
    } else if (key == 0x50) { // Стрелка вниз
        if (history_pos < history_count - 1) {
            history_pos++;
            while (*pos > 0) {
                printf("\b \b");
                (*pos)--;
            }
            strcpy(buffer, history[history_pos % MAX_HISTORY]);
            *pos = strlen(buffer);
            printf("%s", buffer);
        } else if (history_pos == history_count - 1) {
            history_pos = history_count;
            while (*pos > 0) {
                printf("\b \b");
                (*pos)--;
            }
            buffer[0] = '\0';
        }
    }
}

// Главный цикл shell
int main(void) {
    char cmd[MAX_CMD_LEN];
    int pos = 0;
    
    printf("\n");
    printf("========================================\n");
    printf("   Soiav OS 2 - Командная оболочка\n");
    printf("   Введите 'help' для списка команд\n");
    printf("========================================\n\n");
    
    while (1) {
        print_prompt();
        
        // Чтение команды
        pos = 0;
        while (1) {
            int c = getchar();
            
            if (c == '\n') {
                cmd[pos] = '\0';
                printf("\n");
                break;
            } else if (c == '\b' || c == 0x7f) { // Backspace
                if (pos > 0) {
                    printf("\b \b");
                    pos--;
                }
            } else if (c == 0x1b) { // Escape последовательность
                c = getchar(); // '['
                c = getchar(); // Код
                handle_history(c, cmd, &pos);
            } else if (c >= 0x20 && c <= 0x7e) { // Печатные символы
                if (pos < MAX_CMD_LEN - 1) {
                    cmd[pos++] = c;
                    putchar(c);
                }
            }
        }
        
        // Добавляем в историю
        if (cmd[0] != '\0') {
            add_to_history(cmd);
        }
        
        // Проверка на exit
        if (strcmp(cmd, "exit") == 0) {
            printf("Выход из оболочки...\n");
            break;
        }
        
        // Выполнение команды
        execute_command(cmd);
    }
    
    return 0;
}
