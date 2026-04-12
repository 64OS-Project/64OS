#ifndef SCHED_H
#define SCHED_H

#include <kernel/types.h>
#include <fs/vfs.h>
#include <kernel/list.h>

#define KSTACK_SIZE (8 * 1024) /*
 * default size
 */

typedef enum
{
    TASK_RUNNING,
    TASK_READY,
    TASK_BLOCKED,
    TASK_ZOMBIE
} task_state_t;

typedef struct task
{
    int pid;
    int state;
    u64 *regs;
    void *kstack;
    sz kstack_size;
    int exit_code;
    struct task *next;    /*
 * circular task list
 */
    struct task *znext;   /*
 * list of zombies (separate index!)
 */
    void *user_mem;       //pointer to .user memory
    sz user_mem_size;     //.user memory size
    char *name;
    u64 *page_table;
    vfs_inode_t *cwd_inode;     //current directory (inod)
    char cwd_path[256];          //current directory (path)
    void *program_break;        //current heap boundary (program break)
    void *heap_start;           //start of the heap (usually after .bss)
    void *heap_end;             //maximum limit (protection)
} task_t;

extern volatile int need_reschedule;

typedef struct task_info
{
    int pid;
    int state; //TASK_RUNNING, TASK_READY, etc.
    char *name;
} task_info_t;

void scheduler_init(void);
void task_create(void (*entry)(void), sz stack_size, const char *name);
int task_list(task_info_t *buf, sz max);
int task_stop(int pid);
void reap_zombies(void);
void schedule_from_isr(u64 *regs, u64 **out_regs_ptr);

task_t *get_current_task(void);
void task_exit(int exit_code);

u64 utask_create(void (*entry)(void),
                      sz stack_size,
                      void *user_mem,
                      sz user_mem_size,
                      int argc,
                      uptr argv_ptr,
                      const char *name);

int task_is_alive(int pid);

void kill_all_tasks(void);
void emergency_terminate_all(void);
void task_yield(void);

/*
 * ============================================================================== Spinlocks (general definition for the entire core) ===============================================================================
 */

static inline void spinlock_lock(volatile u32 *lock) {
    while (__sync_lock_test_and_set(lock, 1)) {
        while (*lock) {
            asm volatile("pause");
        }
    }
    __sync_synchronize();
}

static inline void spinlock_unlock(volatile u32 *lock) {
    __sync_synchronize();
    __sync_lock_release(lock);
}

static inline void spinlock_lock_irqsave(volatile u32 *lock, u64 *flags) {
    asm volatile("pushfq; popq %0" : "=r"(*flags));
    asm volatile("cli");
    spinlock_lock(lock);
}

static inline void spinlock_unlock_irqrestore(volatile u32 *lock, u64 flags) {
    spinlock_unlock(lock);
    asm volatile("pushq %0; popfq" : : "r"(flags) : "memory", "cc");
}

#endif /*
 * SCHED_H
 */
