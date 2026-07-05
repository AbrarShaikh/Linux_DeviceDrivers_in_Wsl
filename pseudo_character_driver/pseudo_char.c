#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>

#define PSEUDO_BUFFER_SIZE 4096

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Developer");
MODULE_DESCRIPTION("Pseudo character device driver with read/write/seek support");
MODULE_VERSION("0.1");

static char pseudo_buffer[PSEUDO_BUFFER_SIZE];
static size_t pseudo_data_size;
static DEFINE_MUTEX(pseudo_mutex);

static dev_t pseudo_dev_number;
static struct cdev pseudo_cdev;
static struct class *pseudo_class;

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

static int __init pseudo_init(void)
{
    int ret;

    mutex_init(&pseudo_mutex);
    pseudo_data_size = 0;

    ret = alloc_chrdev_region(&pseudo_dev_number, 0, 1, "pseudodev");
    if (ret < 0) {
        pr_err("pseudodev: alloc_chrdev_region failed: %d\n", ret);
        return ret;
    }

    cdev_init(&pseudo_cdev, &pseudo_fops);
    pseudo_cdev.owner = THIS_MODULE;

    ret = cdev_add(&pseudo_cdev, pseudo_dev_number, 1);
    if (ret < 0) {
        pr_err("pseudodev: cdev_add failed: %d\n", ret);
        unregister_chrdev_region(pseudo_dev_number, 1);
        return ret;
    }

    pseudo_class = class_create("pseudo_class");
    if (IS_ERR(pseudo_class)) {
        ret = PTR_ERR(pseudo_class);
        pr_err("pseudodev: class_create failed: %d\n", ret);
        cdev_del(&pseudo_cdev);
        unregister_chrdev_region(pseudo_dev_number, 1);
        return ret;
    }

    if (!device_create(pseudo_class, NULL, pseudo_dev_number, NULL, "pseudodev")) {
        pr_err("pseudodev: device_create failed\n");
        class_destroy(pseudo_class);
        cdev_del(&pseudo_cdev);
        unregister_chrdev_region(pseudo_dev_number, 1);
        return -ENOMEM;
    }

    pr_info("pseudodev: registered major=%d minor=%d\n", MAJOR(pseudo_dev_number), MINOR(pseudo_dev_number));
    return 0;
}

static void __exit pseudo_exit(void)
{
    device_destroy(pseudo_class, pseudo_dev_number);
    class_destroy(pseudo_class);
    cdev_del(&pseudo_cdev);
    unregister_chrdev_region(pseudo_dev_number, 1);
    pr_info("pseudodev: unloaded\n");
}

module_init(pseudo_init);
module_exit(pseudo_exit);
