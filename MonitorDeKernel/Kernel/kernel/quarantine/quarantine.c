#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/namei.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/slab.h>

#define MAX_QUARANTINE 64
#define MAX_PATH_LEN   256

struct quarantine_entry {
    char  path[MAX_PATH_LEN];
    long long timestamp;
};

static struct quarantine_entry q_list[MAX_QUARANTINE];
static int    q_count = 0;
static DEFINE_SPINLOCK(q_lock);

/* ---- sys_quarantine_file ---- */
SYSCALL_DEFINE1(quarantine_file, const char __user *, upath)
{
    char kpath[MAX_PATH_LEN];
    struct path p;
    int i, ret;

    if (!upath) return -EINVAL;
    if (strncpy_from_user(kpath, upath, MAX_PATH_LEN) < 0)
        return -EFAULT;

    /* Verificar que el archivo existe */
    ret = kern_path(kpath, LOOKUP_FOLLOW, &p);
    if (ret) return ret;
    path_put(&p);

    spin_lock(&q_lock);

    /* Verificar duplicados */
    for (i = 0; i < q_count; i++) {
        if (strncmp(q_list[i].path, kpath, MAX_PATH_LEN) == 0) {
            spin_unlock(&q_lock);
            return -EEXIST;
        }
    }

    if (q_count >= MAX_QUARANTINE) {
        spin_unlock(&q_lock);
        return -ENOMEM;
    }

    strncpy(q_list[q_count].path, kpath, MAX_PATH_LEN - 1);
    q_list[q_count].path[MAX_PATH_LEN - 1] = '\0';
    q_list[q_count].timestamp = ktime_get_real_seconds();
    q_count++;

    spin_unlock(&q_lock);
    return 0;
}

/* ---- sys_restore_file ---- */
SYSCALL_DEFINE1(restore_file, const char __user *, upath)
{
    char kpath[MAX_PATH_LEN];
    int  i, found = -1;

    if (!upath) return -EINVAL;
    if (strncpy_from_user(kpath, upath, MAX_PATH_LEN) < 0)
        return -EFAULT;

    spin_lock(&q_lock);
    for (i = 0; i < q_count; i++) {
        if (strncmp(q_list[i].path, kpath, MAX_PATH_LEN) == 0) {
            found = i; break;
        }
    }

    if (found < 0) { spin_unlock(&q_lock); return -ENOENT; }

    /* Eliminar de la lista desplazando */
    for (i = found; i < q_count - 1; i++)
        q_list[i] = q_list[i + 1];
    q_count--;

    spin_unlock(&q_lock);
    return 0;
}

/* ---- sys_get_quarantine_list ---- */
SYSCALL_DEFINE2(get_quarantine_list,
                struct quarantine_entry __user *, ubuf,
                int, max_count)
{
    int copy_count;

    if (!ubuf || max_count <= 0) return -EINVAL;

    spin_lock(&q_lock);
    copy_count = q_count < max_count ? q_count : max_count;
    if (copy_to_user(ubuf, q_list, copy_count * sizeof(struct quarantine_entry))) {
        spin_unlock(&q_lock);
        return -EFAULT;
    }
    spin_unlock(&q_lock);

    return copy_count;
}
