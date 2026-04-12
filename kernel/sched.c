#include <kernel/sched.h>
#include <mm/heap.h>
#include <libk/string.h>
#include <kernel/panic.h>
#include <kernel/types.h>
#include <kernel/tss.h>
#include <kernel/paging.h>
#include <kernel/terminal.h>

#define USER_CS ((u64)0x18 | 3) /*
 * 0x1B
 */
#define USER_SS ((u64)0x20 | 3) /*
 * 0x23
 */

extern char _heap_start;
extern char _heap_end;

u64 g_syscall_kstack_top = 0;

static u8 init_task_stack[16 * 1024];
static task_t *task_ring = NULL; /*
 * tail (last element)
 */
static task_t *current = NULL;
static int next_pid = 1;

/*
 * Static init task to avoid calling malloc in the ISR
 */
static task_t init_task;

/*
 * List of zombies for deferred cleanup
 */
static task_t *zombie_list = NULL;

volatile int need_reschedule = 0;

/*
 * CLI/STI
 */
static inline void cli(void) { __asm__ volatile("cli" ::: "memory"); }
static inline void sti(void) { __asm__ volatile("sti" ::: "memory"); }

/*
 * prepare_initial_stack: layout exactly matches your ISR push order
 */
static u64 *prepare_initial_stack(void (*entry)(void),
                                       void *kstack_top,
                                       void *user_stack_top,
                                       int argc,
                                       uptr argv_ptr,
                                       int user_mode)
{
    const int FRAME_QWORDS = 22;
    u64 *sp = (u64 *)kstack_top;
    sp = (u64 *)(((uptr)sp) & ~0xFULL); /*
 * align down 16
 */
    sp -= FRAME_QWORDS;

    sp[0] = 32;                  /*
 * int_no (dummy)
 */
    sp[1] = 0;                   /*
 * err_code
 */
    sp[2] = 0;                   /*
 * r15
 */
    sp[3] = 0;                   /*
 * r14
 */
    sp[4] = 0;                   /*
 * r13
 */
    sp[5] = 0;                   /*
 * r12
 */
    sp[6] = 0;                   /*
 * r11
 */
    sp[7] = 0;                   /*
 * r10
 */
    sp[8] = 0;                   /*
 * r9
 */
    sp[9] = 0;                   /*
 * r8
 */
    sp[10] = (u64)argc;     /*
 * rdi
 */
    sp[11] = (u64)argv_ptr; /*
 * rsi
 */
    sp[12] = 0;                  /*
 * rbp
 */
    sp[13] = 0;                  /*
 * rbx
 */
    sp[14] = 0;                  /*
 * rdx
 */
    sp[15] = 0;                  /*
 * rcx
 */
    sp[16] = 0;                  /*
 * rax
 */
    sp[17] = (u64)entry;    /*
 * rip
 */
    sp[19] = 0x202;              /*
 * rflags (IF=1)
 */

    if (user_mode)
    {
        sp[18] = USER_CS;
        sp[20] = (u64)user_stack_top;
        sp[21] = USER_SS;
    }
    else
    {
        sp[18] = 0x08;
        sp[20] = (u64)kstack_top;
        sp[21] = 0x10;
    }

    return sp;
}

char *strdup(const char *s)
{
    if (!s)
        return NULL;

    sz len = strlen(s) + 1; //+1 for '\0'
    char *dup = (char *)malloc(len);

    if (!dup)
        return NULL;

    memcpy(dup, s, len);
    return dup;
}

/*
 * ----------------Scheduler init / create / pick_next --------------------
 */
void scheduler_init(void)
{
    memset(&init_task, 0, sizeof(init_task));
    init_task.pid = 0;
    init_task.state = TASK_RUNNING;
    init_task.name = "INIT";
    init_task.regs = NULL;
    init_task.kstack = NULL;
    init_task.kstack_size = 0;
    init_task.next = &init_task;

    task_ring = &init_task;
    current = NULL;
    next_pid = 1;

    /*
 * Initializing TSS
 */
    tss_init();
}

/*
 * Creates a kernel-thread
 */
void task_create(void (*entry)(void), sz stack_size, const char *name)
{
    if (!entry)
    {
        panic("TASK_CREATE_NULL_ENTRY");
    }
    static volatile int sched_lock = 0;
    spinlock_lock(&sched_lock);

    if (stack_size == 0)
        stack_size = KSTACK_SIZE;

    task_t *t = (task_t *)malloc(sizeof(task_t));
    if (!t)
    {
        panic("TASK_ALLOCATION_FAILED");
    }

    void *kstack = malloc(stack_size);
    if (!kstack)
    {
        free(t);
        panic("TASK_STACK_ALLOCATION_FAILED");
    }

    memset(t, 0, sizeof(*t));
    t->pid = next_pid++;
    t->state = TASK_READY;
    t->kstack = kstack;
    t->kstack_size = stack_size;
    t->exit_code = 0;
    t->next = NULL;
    t->name = strdup(name);
    t->page_table = NULL;

    void *kstack_top = (char *)kstack + stack_size;
    t->regs = prepare_initial_stack(entry,
                                    kstack_top,
                                    kstack_top, /*
 * kernel uses kstack
 */
                                    0,
                                    0,
                                    0); /*
 * kernel mode
 */

    /*
 * Insert into the ring as a new tail
 */
    cli();
    if (!task_ring)
    {
        task_ring = t;
        t->next = t;
    }
    else
    {
        t->next = task_ring->next;
        task_ring->next = t;
        task_ring = t;
    }
    sti();
    spinlock_unlock(&sched_lock);
}

/*
 * Simple selection of the next READY task (round-robin).
 */
static task_t *pick_next(void)
{
    if (!task_ring)
        return NULL;

    task_t *start = current ? current->next : task_ring->next;
    task_t *it = start;
    do
    {
        if (it->state == TASK_READY || it->state == TASK_RUNNING)
            return it;
        it = it->next;
    } while (it != start);

    return NULL;
}

void schedule_from_isr(u64 *regs, u64 **out_regs_ptr)
{
    if (!regs)
    {
        panic("SCHEDULER_NULL_REGS");
    }

    if (!current)
    {
        init_task.kstack = init_task_stack;
        init_task.kstack_size = sizeof(init_task_stack);
        init_task.regs = regs;
        init_task.state = TASK_RUNNING;
        current = &init_task;
        g_syscall_kstack_top = (u64)current->kstack + current->kstack_size;
        tss_update_rsp0(g_syscall_kstack_top);
        *out_regs_ptr = current->regs;
        need_reschedule = 0;  //Resetting the flag
        return;
    }
    
    //Saving the context of the current task
    current->regs = regs;
    
    //If the task is not blocked and is not a zombie, transfer it to READY
    if (current->state == TASK_RUNNING) {
        current->state = TASK_READY;
    }
    
    //Checking to see if you need to switch
    if (need_reschedule) {
        task_t *next = pick_next();
        if (next && next != current) {
            current = next;
        }
        need_reschedule = 0;
    }
    
    current->state = TASK_RUNNING;
    *out_regs_ptr = current->regs;
    g_syscall_kstack_top = (u64)current->kstack + current->kstack_size;
    tss_update_rsp0(g_syscall_kstack_top);
    paging_switch(current->page_table);
}

task_t *get_current_task(void) { return current; }

static void add_to_zombie_list(task_t *t)
{
    if (!t)
        return;
    t->znext = zombie_list;
    zombie_list = t;
}

static int unlink_from_ring(task_t *t)
{
    if (!task_ring || !t)
        return -1;

    if (task_ring->next == task_ring)
    {
        if (task_ring == t)
        {
            if (&init_task == t)
                return -1;
            task_ring = &init_task;
            init_task.next = &init_task;
            return 0;
        }
        return -1;
    }

    task_t *prev = task_ring;
    task_t *it = task_ring->next;
    do
    {
        if (it == t)
        {
            prev->next = it->next;
            if (task_ring == it)
                task_ring = prev;
            return 0;
        }
        prev = it;
        it = it->next;
    } while (it != task_ring->next);

    return -1;
}

static void free_task_resources(task_t *t)
{
    if (!t || t == &init_task)
        return;

    if (t->kstack)
        free(t->kstack);

    if (t->user_mem)
    {
        free(t->user_mem);
        t->user_mem = NULL;
        t->user_mem_size = 0;
    }

    if (t->name)
    { //Freeing the memory allocated for the name
        free(t->name);
        t->name = NULL;
    }
    
    if (t->page_table)
    {
    	paging_destroy_user_task(t->page_table);
    	t->page_table = NULL;
    }

    free(t);
}

void reap_zombies(void)
{
    cli();
    task_t *z = zombie_list;
    zombie_list = NULL;
    sti();

    while (z)
    {
        task_t *next_z = z->znext;
        cli();
        unlink_from_ring(z);
        sti();
        free_task_resources(z);
        z = next_z;
    }
}

int task_list(task_info_t *buf, sz max)
{
    cli();
    if (!task_ring)
    {
        sti();
        return 0;
    }

    int count = 0;
    task_t *it = task_ring->next;
    do
    {
        if (count >= (int)max)
            break;
        buf[count].pid = it->pid;
        buf[count].state = it->state;
        buf[count].name = strdup(it->name);
        count++;
        it = it->next;
    } while (it != task_ring->next);

    sti();
    return count;
}

int task_stop(int pid)
{
    if (pid == 0) {
    }

    reap_zombies();

    cli();
    if (!task_ring)
    {
        sti();
        return -1;
    }

    task_t *it = task_ring->next;
    task_t *found = NULL;
    do
    {
        if (it->pid == pid)
        {
            found = it;
            break;
        }
        it = it->next;
    } while (it != task_ring->next);

    if (!found)
    {
        sti();
        return -1;
    }

    if (found == current)
    {
        current->state = TASK_ZOMBIE;
        add_to_zombie_list(current);
        unlink_from_ring(current);
        sti();
        for (;;)
        {
            sti();
            __asm__ volatile("hlt");
        }
    }

    unlink_from_ring(found);
    sti();
    free_task_resources(found);
    return 0;
}

void task_exit(int exit_code)
{
    reap_zombies();

    cli();
    if (!current || current == &init_task)
    {
        sti();
        return;
    }

    current->exit_code = exit_code;
    current->state = TASK_ZOMBIE;
    add_to_zombie_list(current);
    unlink_from_ring(current);
    sti();

    for (;;)
    {
        sti();
        __asm__ volatile("hlt");
    }
}

u64 utask_create(void (*entry)(void),
                      sz stack_size,
                      void *user_mem,
                      sz user_mem_size,
                      int argc,
                      uptr argv_ptr,
                      const char *name)
{
    static volatile int sched_lock = 0;
    spinlock_lock(&sched_lock);
    if (!entry)
    {
        panic("UTASK_CREATE_NULL_ENTRY");
    }

    if (!user_mem || user_mem_size == 0)
    {
        panic("UTASK_CREATE_INVALID_MEMORY");
    }

    if (stack_size == 0)
        stack_size = KSTACK_SIZE;

    task_t *t = (task_t *)malloc(sizeof(task_t));
    if (!t)
    {
        return 0;
    }

    void *kstack = malloc(stack_size);
    if (!kstack)
    {
        free(t);
        return 0;
    }

    memset(t, 0, sizeof(*t));
    t->pid = next_pid++;
    t->state = TASK_READY;
    t->kstack = kstack;
    t->kstack_size = stack_size;
    t->user_mem = user_mem;
    t->heap_start = user_mem;                    //start of the heap
    t->program_break = user_mem;                  //empty for now
    t->heap_end = (char*)user_mem + user_mem_size; //end of selection
    t->user_mem_size = user_mem_size;
    t->name = strdup(name);
    t->page_table = paging_create_user_task(user_mem, user_mem_size);
    if (!t->page_table)
    {
        //Page tables could not be created - the task cannot be run.
        free(kstack);
        free(t);
        return 0;
    }

    void *user_stack_top = (char *)user_mem + user_mem_size;
    void *kstack_top = (char *)kstack + stack_size;

    /*
 * Pass user_mode=1 and user_stack_top
 */
    t->regs = prepare_initial_stack(entry,
                                    kstack_top,
                                    user_stack_top,
                                    argc,
                                    argv_ptr,
                                    1); /*
 * ← User mode!
 */
    t->exit_code = 0;

    cli();
    if (!task_ring)
    {
        task_ring = t;
        t->next = t;
    }
    else
    {
        t->next = task_ring->next;
        task_ring->next = t;
        task_ring = t;
    }
    sti();

    return t->pid;
    spinlock_unlock(&sched_lock);
}

int task_is_alive(int pid)
{
    if (pid < 0)
        return 0;

    if (pid == 0)
        return 1;

    reap_zombies();

    cli();
    if (!task_ring)
    {
        sti();
        return 0;
    }

    task_t *it = task_ring->next;
    do
    {
        if (it->pid == pid)
        {
            int alive = (it->state != TASK_ZOMBIE);
            sti();
            return alive;
        }
        it = it->next;
    } while (it != task_ring->next);

    sti();
    return 0;
}

void kill_all_tasks(void)
{
    cli(); //Disable interrupts

    if (!task_ring)
    {
        sti();
        return;
    }

    task_t *start = task_ring->next;
    task_t *it = start;

    do
    {
        //Skip the init task (PID 0)
        if (it->pid != 0 && it != &init_task)
        {
            //Marked as zombie
            it->state = TASK_ZOMBIE;
            it->exit_code = -1; //Error exit code

            //Adding zombies to the list for cleaning
            add_to_zombie_list(it);
        }
        it = it->next;
    } while (it != start);

    //Removing all completed tasks from the ring
    reap_zombies();

    //The current task becomes init
    current = &init_task;
    current->state = TASK_RUNNING;

    //Renew the ring if necessary
    if (task_ring->pid != 0)
    {
        task_ring = &init_task;
        init_task.next = &init_task;
    }

    sti();
}

/*
 * Immediately completes all tasks, freeing resources
 */
void emergency_terminate_all(void)
{
    cli();

    if (!task_ring)
    {
        sti();
        return;
    }

    //Saving a pointer to the beginning
    task_t *start = task_ring->next;
    task_t *it = start;

    //Create a temporary array for tasks that need to be deleted
    task_t *tasks_to_free[256];
    int task_count = 0;

    //First we collect all tasks except init
    do
    {
        if (it->pid != 0 && it != &init_task)
        {
            if (task_count < 256)
            {
                tasks_to_free[task_count++] = it;
            }
        }
        it = it->next;
    } while (it != start && task_count < 256);

    //Remove all found tasks from the ring
    for (int i = 0; i < task_count; i++)
    {
        unlink_from_ring(tasks_to_free[i]);
    }

    //Immediately release resources
    for (int i = 0; i < task_count; i++)
    {
        free_task_resources(tasks_to_free[i]);
    }

    //Resetting structures
    task_ring = &init_task;
    init_task.next = &init_task;
    current = &init_task;
    current->state = TASK_RUNNING;

    //Clearing the zombie list
    zombie_list = NULL;

    sti();
}
