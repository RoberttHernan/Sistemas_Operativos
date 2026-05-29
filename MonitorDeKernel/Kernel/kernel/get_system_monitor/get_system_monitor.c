#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/vmstat.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <linux/rcupdate.h>

#define TOP_PROCS 10

struct top_proc {
    int pid;
    char name[16];
    unsigned long mem_kb;
    unsigned int mem_pct_x100;
};

struct system_monitor_info {
    unsigned long long memoria_total;
    unsigned long long memoria_usada;
    unsigned long long memoria_libre;
    unsigned long long memoria_cache;
    unsigned long long swap_total;
    unsigned long long swap_usada;
    unsigned long long fallos_menores;
    unsigned long long fallos_mayores;
    unsigned long long paginas_activas;
    unsigned long long paginas_inactivas;
    struct top_proc top_processes[TOP_PROCS];
    int top_count;
};

static void insert_top(struct top_proc top[], int *count,
                       int pid, const char *name,
                       unsigned long mem_kb, unsigned long total_kb)
{
    struct top_proc e;
    int i, pos;

    e.pid = pid;
    e.mem_kb = mem_kb;
    e.mem_pct_x100 = total_kb ? (unsigned int)(mem_kb * 10000UL / total_kb) : 0;
    strncpy(e.name, name, 15);
    e.name[15] = '\0';

    pos = *count;
    for (i = 0; i < *count; i++) {
        if (e.mem_kb > top[i].mem_kb) { pos = i; break; }
    }
    if (pos >= TOP_PROCS) return;
    if (*count < TOP_PROCS) (*count)++;
    for (i = *count - 1; i > pos; i--)
        top[i] = top[i - 1];
    top[pos] = e;
}

SYSCALL_DEFINE1(get_system_monitor, struct system_monitor_info __user *, uinfo)
{
    struct system_monitor_info info;
    struct sysinfo si;
    struct task_struct *task;
    unsigned long page_kb = PAGE_SIZE / 1024;

    if (!uinfo) return -EINVAL;
    memset(&info, 0, sizeof(info));

    si_meminfo(&si);
    info.memoria_total    = (unsigned long long)si.totalram  * si.mem_unit / 1024;
    info.memoria_libre    = (unsigned long long)si.freeram   * si.mem_unit / 1024;
    info.swap_total       = (unsigned long long)si.totalswap * si.mem_unit / 1024;
    info.swap_usada       = (unsigned long long)(si.totalswap - si.freeswap) * si.mem_unit / 1024;
    info.memoria_cache    = (unsigned long long)global_node_page_state(NR_FILE_PAGES) * page_kb;
    info.memoria_usada    = info.memoria_total > info.memoria_libre + info.memoria_cache
                          ? info.memoria_total - info.memoria_libre - info.memoria_cache : 0;
    info.paginas_activas  = global_node_page_state(NR_ACTIVE_ANON)
                          + global_node_page_state(NR_ACTIVE_FILE);
    info.paginas_inactivas= global_node_page_state(NR_INACTIVE_ANON)
                          + global_node_page_state(NR_INACTIVE_FILE);
    info.fallos_mayores   = (unsigned long long)global_node_page_state(PGMAJFAULT);
    info.fallos_menores   = (unsigned long long)global_node_page_state(PGFAULT)
                          - info.fallos_mayores;

    rcu_read_lock();
    for_each_process(task) {
        struct mm_struct *mm;
        unsigned long rss = 0;
        char comm[TASK_COMM_LEN];
        if (!task->mm) continue;
        mm = get_task_mm(task);
        if (mm) { rss = get_mm_rss(mm) * page_kb; mmput(mm); }
        get_task_comm(comm, task);
        insert_top(info.top_processes, &info.top_count,
                   task->pid, comm, rss, (unsigned long)info.memoria_total);
    }
    rcu_read_unlock();

    if (copy_to_user(uinfo, &info, sizeof(info)))
        return -EFAULT;
    return 0;
}
