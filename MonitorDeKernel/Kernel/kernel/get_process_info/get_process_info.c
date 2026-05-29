#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/rcupdate.h>
#include <linux/sched/signal.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>

struct process_info {
    int pid;
    char name[16];
    long cpu_time;
    long mem_kb;
};

SYSCALL_DEFINE2(get_process_info, int, pid, struct process_info __user *, uinfo)
{
    struct task_struct *task;
    struct process_info info;
    struct mm_struct *mm;

    if (!uinfo)
        return -EINVAL;

    rcu_read_lock();
    task = find_task_by_vpid(pid);
    if (!task) {
        rcu_read_unlock();
        return -ESRCH;
    }

    info.pid = task->pid;
    get_task_comm(info.name, task);
    info.cpu_time = (long)(task->utime + task->stime);

    mm = get_task_mm(task);
    if (mm) {
        info.mem_kb = (long)(get_mm_rss(mm) * (PAGE_SIZE / 1024));
        mmput(mm);
    } else {
        info.mem_kb = 0;
    }

    rcu_read_unlock();

    if (copy_to_user(uinfo, &info, sizeof(info)))
        return -EFAULT;

    return 0;
}
