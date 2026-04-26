#ifndef ERRLIST_H
#define ERRLIST_H

#include <kernel/types.h>
#include <ktime/clock.h>

#define ERRLIST_MAX_ENTRIES     4096
#define ERRLIST_MAX_SUBSYSTEM    4096
#define ERRLIST_MAX_MESSAGE      256

typedef struct {
    char subsystem[ERRLIST_MAX_SUBSYSTEM];  // Название подсистемы (USB, ACPI, FS, etc)
    char message[ERRLIST_MAX_MESSAGE];       // Текст ошибки
    u32 code;                                // Код ошибки (если есть)
    ClockTime timestamp;                      // Время возникновения
    bool is_error;                           // true = error, false = warning
    u32 line;                                // Строка в коде (опционально)
    char file[64];                           // Файл (опционально)
} errlist_entry_t;

typedef struct {
    errlist_entry_t entries[ERRLIST_MAX_ENTRIES];
    u32 count;
    bool enabled;
} errlist_t;

// Инициализация
void errlist_init(void);

// Добавление ошибки/предупреждения
void errlist_add(const char *subsystem, const char *message, u32 code, bool is_error);
void errlist_add_ext(const char *subsystem, const char *message, u32 code, 
                     bool is_error, const char *file, u32 line);

// Управление
void errlist_clear(void);
void errlist_clear_by_subsystem(const char *subsystem);
void errlist_clear_by_code(u32 code);

// Поиск и фильтрация
errlist_entry_t* errlist_find_by_subsystem(const char *subsystem, u32 *count);
errlist_entry_t* errlist_find_by_code(u32 code, u32 *count);
errlist_entry_t* errlist_find_by_message(const char *substr, u32 *count);

// Вывод
void errlist_show_all(void);
void errlist_show_by_subsystem(const char *subsystem);
void errlist_show_by_code(u32 code);
void errlist_show_errors_only(void);
void errlist_show_warnings_only(void);
void errlist_show_last(u32 n);

// Статистика
u32 errlist_get_count(void);
u32 errlist_get_error_count(void);
u32 errlist_get_warning_count(void);

// Глобальный список ошибок
extern errlist_t g_errlist;

#endif