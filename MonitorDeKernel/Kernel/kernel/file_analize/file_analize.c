#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/stat.h>
#include <linux/crypto.h>
#include <crypto/hash.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>

struct file_info {
    long long size;
    long long last_modified;
    char sha256[65];
};

static int calc_sha256(const char *path, char *out_hex)
{
    struct crypto_shash *tfm;
    struct shash_desc   *desc;
    struct file *f;
    unsigned char digest[32];
    unsigned char *buf;
    ssize_t bytes;
    loff_t pos = 0;
    int ret = 0;
    int i;

    tfm = crypto_alloc_shash("sha256", 0, 0);
    if (IS_ERR(tfm)) return PTR_ERR(tfm);

    desc = kmalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
    if (!desc) { crypto_free_shash(tfm); return -ENOMEM; }
    desc->tfm = tfm;

    buf = kmalloc(4096, GFP_KERNEL);
    if (!buf) { kfree(desc); crypto_free_shash(tfm); return -ENOMEM; }

    f = filp_open(path, O_RDONLY, 0);
    if (IS_ERR(f)) { ret = PTR_ERR(f); goto out; }

    crypto_shash_init(desc);
    while ((bytes = kernel_read(f, buf, 4096, &pos)) > 0)
        crypto_shash_update(desc, buf, bytes);
    crypto_shash_final(desc, digest);
    filp_close(f, NULL);

    for (i = 0; i < 32; i++)
        sprintf(out_hex + i * 2, "%02x", digest[i]);
    out_hex[64] = '\0';

out:
    kfree(buf);
    kfree(desc);
    crypto_free_shash(tfm);
    return ret;
}

SYSCALL_DEFINE2(file_analize, const char __user *, upath,
                struct file_info __user *, uinfo)
{
    struct file_info info;
    struct path      kpath;
    struct kstat     stat;
    char             *kbuf;
    int              ret;

    if (!upath || !uinfo) return -EINVAL;

    kbuf = kmalloc(PATH_MAX, GFP_KERNEL);
    if (!kbuf) return -ENOMEM;

    if (strncpy_from_user(kbuf, upath, PATH_MAX) < 0) {
        kfree(kbuf); return -EFAULT;
    }

    ret = kern_path(kbuf, LOOKUP_FOLLOW, &kpath);
    if (ret) { kfree(kbuf); return ret; }

    ret = vfs_getattr(&kpath, &stat, STATX_SIZE | STATX_MTIME, 0);
    path_put(&kpath);
    if (ret) { kfree(kbuf); return ret; }

    memset(&info, 0, sizeof(info));
    info.size          = (long long)stat.size;
    info.last_modified = (long long)stat.mtime.tv_sec;

    ret = calc_sha256(kbuf, info.sha256);
    kfree(kbuf);
    if (ret) return ret;

    if (copy_to_user(uinfo, &info, sizeof(info)))
        return -EFAULT;

    return 0;
}
