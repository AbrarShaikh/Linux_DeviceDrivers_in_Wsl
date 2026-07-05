#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>

#define PSEUDO_BUFFER_SIZE 4096

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Developer");
MODULE_DESCRIPTION("Pseudo miscellaneous character device driver with read/write/seek support");
MODULE_VERSION("0.1");

static char pseudo_buffer[PSEUDO_BUFFER_SIZE];
static size_t pseudo_data_size;
static DEFINE_MUTEX(pseudo_mutex);

static ssize_t pseudo_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    ssize_t available;
    ssize_t to_copy;

    if (mutex_lock_interruptible(&pseudo_mutex))
        return -ERESTARTSYS;

    if (*ppos < 0) {
        mutex_unlock(&pseudo_mutex);
        return -EINVAL;
    }

    if (*ppos >= pseudo_data_size) {
        mutex_unlock(&pseudo_mutex);
        return 0;
    }

    available = pseudo_data_size - *ppos;
    to_copy = min((ssize_t)count, available);

    if (copy_to_user(buf, pseudo_buffer + *ppos, to_copy)) {
        mutex_unlock(&pseudo_mutex);
        return -EFAULT;
    }

    *ppos += to_copy;
    mutex_unlock(&pseudo_mutex);
    return to_copy;
}

static ssize_t pseudo_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    ssize_t to_copy;

    if (mutex_lock_interruptible(&pseudo_mutex))
        return -ERESTARTSYS;

    if (*ppos < 0) {
        mutex_unlock(&pseudo_mutex);
        return -EINVAL;
    }

    if (*ppos >= PSEUDO_BUFFER_SIZE) {
        mutex_unlock(&pseudo_mutex);
        return -ENOSPC;
    }

    to_copy = min((ssize_t)count, (ssize_t)(PSEUDO_BUFFER_SIZE - *ppos));

    if (copy_from_user(pseudo_buffer + *ppos, buf, to_copy)) {
        mutex_unlock(&pseudo_mutex);
        return -EFAULT;
    }

    *ppos += to_copy;
    pseudo_data_size = max(pseudo_data_size, (size_t)*ppos);
    mutex_unlock(&pseudo_mutex);
    return to_copy;
}

static loff_t pseudo_llseek(struct file *file, loff_t offset, int whence)
{
    loff_t newpos;

    if (mutex_lock_interruptible(&pseudo_mutex))
        return -ERESTARTSYS;

    switch (whence) {
    case SEEK_SET:
        newpos = offset;
        break;
    case SEEK_CUR:
        newpos = file->f_pos + offset;
        break;
    case SEEK_END:
        newpos = pseudo_data_size + offset;
        break;
    default:
        mutex_unlock(&pseudo_mutex);
        return -EINVAL;
    }

    if (newpos < 0 || newpos > PSEUDO_BUFFER_SIZE) {
        mutex_unlock(&pseudo_mutex);
        return -EINVAL;
    }

    file->f_pos = newpos;
    mutex_unlock(&pseudo_mutex);
    return newpos;
}

static int pseudo_open(struct inode *inode, struct file *file)
{
    return 0;
}

static int pseudo_release(struct inode *inode, struct file *file)
{
    return 0;
}

static const struct file_operations pseudo_fops = {
    .owner = THIS_MODULE,
    .open = pseudo_open,
    .release = pseudo_release,
    .read = pseudo_read,
    .write = pseudo_write,
    .llseek = pseudo_llseek,
};

static struct miscdevice pseudo_misc_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "pseudomisc",
    .fops = &pseudo_fops,
};

static int __init pseudo_misc_init(void)
{
    int ret;

    mutex_init(&pseudo_mutex);
    pseudo_data_size = 0;

    ret = misc_register(&pseudo_misc_device);
    if (ret) {
        pr_err("pseudomisc: misc_register failed: %d\n", ret);
        return ret;
    }

    pr_info("pseudomisc: registered as /dev/%s\n", pseudo_misc_device.name);
    return 0;
}

static void __exit pseudo_misc_exit(void)
{
    misc_deregister(&pseudo_misc_device);
    pr_info("pseudomisc: unloaded\n");
}

module_init(pseudo_misc_init);
module_exit(pseudo_misc_exit);
