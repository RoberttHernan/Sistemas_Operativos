#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/sched.h>
#include <linux/pid.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/jiffies.h>

/* Estructura que se llena con info del proceso */
struct process_info {
    pid_t   pid;
    char    name[16];       /* TASK_COMM_LEN = 16 */
    long    runtime_sec;    /* segundos de CPU usados */
    long    mem_kb;         /* memoria aproximada en KB */
};

SYSCALL_DEFINE2(get_process_info,
                pid_t,                   pid,
                struct process_info __user *, info)
{
    struct task_struct  *task;
    struct process_info kinfo;
    unsigned long       total_time;

    /* Registrar llamada en el log del kernel (dmesg) */
    printk(KERN_INFO "sys_get_process_info: consultando PID %d\n", pid);

    /* Buscar el proceso por PID */
    rcu_read_lock();
    task = find_task_by_vpid(pid);

    if (!task) {
        rcu_read_unlock();
        return -ESRCH;   /* proceso no encontrado */
    }

    /* Llenar la estructura */
    kinfo.pid = task->pid;
    get_task_comm(kinfo.name, task);   /* nombre del proceso */

    /* Tiempo de CPU en segundos (utime + stime → jiffies → segundos) */
    total_time = task->utime + task->stime;
    kinfo.runtime_sec = (long)(total_time / HZ);

    /* Memoria: total_vm en páginas → KB */
    if (task->mm)
        kinfo.mem_kb = (long)(task->mm->total_vm << (PAGE_SHIFT - 10));
    else
        kinfo.mem_kb = 0;

    rcu_read_unlock();

    /* Copiar al espacio de usuario */
    if (copy_to_user(info, &kinfo, sizeof(kinfo)))
        return -EFAULT;

    return 0;
}
