#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>

struct process_scan_entry {
    int  pid;
    char name[16];
    unsigned long mem_kb;
    unsigned long long cpu_time;
};

SYSCALL_DEFINE2(scan_processes,
                struct process_scan_entry __user *, ubuf,
                int, max_count)
{
    struct process_scan_entry *kbuf;
    struct task_struct *task;
    int count = 0;
    unsigned long page_kb = PAGE_SIZE / 1024;

    if (!ubuf || max_count <= 0) return -EINVAL;
    if (max_count > 512) max_count = 512;

    kbuf = kmalloc_array(max_count, sizeof(*kbuf), GFP_KERNEL);
    if (!kbuf) return -ENOMEM;

    rcu_read_lock();
    for_each_process(task) {
        struct mm_struct *mm;
        if (count >= max_count) break;

        kbuf[count].pid      = task->pid;
        kbuf[count].cpu_time = task->utime + task->stime;
        get_task_comm(kbuf[count].name, task);

        mm = get_task_mm(task);
        if (mm) {
            kbuf[count].mem_kb = get_mm_rss(mm) * page_kb;
            mmput(mm);
        } else {
            kbuf[count].mem_kb = 0;
        }
        count++;
    }
    rcu_read_unlock();

    if (copy_to_user(ubuf, kbuf, count * sizeof(*kbuf))) {
        kfree(kbuf);
        return -EFAULT;
    }

    kfree(kbuf);
    return count;
}
