#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>

#include <linux/kdev_t.h>
#undef pr_fmt
#define pr_fmt(fmt) "%s: " fmt,__func__

/* Device buffer size limit */
#define PSEUDO_BUFFER_SIZE 4096

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Developer");
MODULE_DESCRIPTION("Pseudo character device driver with read/write/seek support");
MODULE_VERSION("0.1");

/* Global state of the pseudo device */
static char pseudo_buffer[PSEUDO_BUFFER_SIZE];  /* Static memory storage */
static size_t pseudo_data_size;                 /* Tracks logical EOF (max offset written) */
static DEFINE_MUTEX(pseudo_mutex);              /* Serializes concurrent read/write/seek accesses */

/* Standard cdev registration structures */
static dev_t pseudo_dev_number;                 /* Major/minor device number allocation */
static struct cdev pseudo_cdev;                 /* Character device structure */
static struct class *pseudo_class;              /* Device class structure under sysfs */

/**
 * pseudo_read() - Read data from the pseudo device buffer to user space.
 * @file: File structure pointer representing the opened device descriptor
 * @buf: Target user space buffer pointer
 * @count: Maximum number of bytes to read
 * @ppos: Pointer to current position in the device file
 *
 * Return: Number of bytes successfully read on success, negative error code on failure.
 */
static ssize_t pseudo_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    size_t available;
    size_t to_copy;

    /* Acquire mutex; allow interruptible sleeps (e.g. Ctrl+C) */
    if (mutex_lock_interruptible(&pseudo_mutex))
        return -ERESTARTSYS;

    /* Ensure current file offset is not negative */
    if (*ppos < 0) {
        pr_err("negative offset %lld\n", (long long)*ppos);
        mutex_unlock(&pseudo_mutex);
        return -EINVAL;
    }

    /* Return EOF (0) if reading past the current logical data size */
    if (*ppos >= pseudo_data_size) {
        pr_info("EOF reached (pos=%lld, data_size=%zu)\n", (long long)*ppos, pseudo_data_size);
        mutex_unlock(&pseudo_mutex);
        return 0;
    }

    /* Calculate remaining bytes available from the current position to logical EOF */
    available = pseudo_data_size - *ppos;
    to_copy = min(count, available);

    /* Safely transfer memory from kernel space to user space */
    if (copy_to_user(buf, pseudo_buffer + *ppos, to_copy)) {
        pr_err("copy_to_user failed for pos=%lld, count=%zu\n", (long long)*ppos, to_copy);
        mutex_unlock(&pseudo_mutex);
        return -EFAULT;
    }

    /* Advance file pointer offset and release mutex lock */
    *ppos += to_copy;
    mutex_unlock(&pseudo_mutex);
    pr_info("read %zu bytes, new pos=%lld, data_size=%zu\n", to_copy, (long long)*ppos, pseudo_data_size);
    return to_copy;
}

/**
 * pseudo_write() - Write data from user space to the pseudo device buffer.
 * @file: File structure pointer representing the opened device descriptor
 * @buf: Source user space buffer pointer containing data to write
 * @count: Number of bytes to write
 * @ppos: Pointer to current position in the device file
 *
 * Return: Number of bytes successfully written on success, negative error code on failure.
 */
static ssize_t pseudo_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    size_t to_copy;

    /* Acquire mutex to prevent concurrent modification of buffer and tracking state */
    if (mutex_lock_interruptible(&pseudo_mutex))
        return -ERESTARTSYS;

    /* Ensure current file offset is not negative */
    if (*ppos < 0) {
        pr_err("negative offset %lld\n", (long long)*ppos);
        mutex_unlock(&pseudo_mutex);
        return -EINVAL;
    }

    /* Return No Space left on device if offset starts at or past buffer limit */
    if (*ppos >= PSEUDO_BUFFER_SIZE) {
        pr_warn("write attempt past buffer limit (pos=%lld, limit=%d)\n", (long long)*ppos, PSEUDO_BUFFER_SIZE);
        mutex_unlock(&pseudo_mutex);
        return -ENOSPC;
    }

    /* Clamp write size to avoid exceeding the physical static buffer size */
    to_copy = min(count, (size_t)(PSEUDO_BUFFER_SIZE - *ppos));

    /* Safely copy payload from user address space to kernel address space */
    if (copy_from_user(pseudo_buffer + *ppos, buf, to_copy)) {
        pr_err("copy_from_user failed for pos=%lld, count=%zu\n", (long long)*ppos, to_copy);
        mutex_unlock(&pseudo_mutex);
        return -EFAULT;
    }

    /* Update offset and dynamically expand the logical end-of-file tracking */
    *ppos += to_copy;
    pseudo_data_size = max(pseudo_data_size, (size_t)*ppos);
    
    mutex_unlock(&pseudo_mutex);
    pr_info("wrote %zu bytes, new pos=%lld, data_size=%zu\n", to_copy, (long long)*ppos, pseudo_data_size);
    return to_copy;
}

/**
 * pseudo_llseek() - Reposition file read/write offset.
 * @file: File structure pointer
 * @offset: Relative displacement in bytes
 * @whence: Base directive (SEEK_SET, SEEK_CUR, or SEEK_END)
 *
 * Return: New position in file on success, negative error code on failure.
 */
static loff_t pseudo_llseek(struct file *file, loff_t offset, int whence)
{
    loff_t newpos;

    /* Acquire mutex lock to ensure atomic update of file position */
    if (mutex_lock_interruptible(&pseudo_mutex))
        return -ERESTARTSYS;

    switch (whence) {
    case SEEK_SET:
        /* Absolute position from start of device */
        newpos = offset;
        break;
    case SEEK_CUR:
        /* Position relative to current file location */
        newpos = file->f_pos + offset;
        break;
    case SEEK_END:
        /* Position relative to current logical end-of-file size */
        newpos = pseudo_data_size + offset;
        break;
    default:
        pr_err("invalid whence=%d\n", whence);
        mutex_unlock(&pseudo_mutex);
        return -EINVAL;
    }

    /* Bound check: ensure position is within [0, PSEUDO_BUFFER_SIZE] */
    if (newpos < 0 || newpos > PSEUDO_BUFFER_SIZE) {
        pr_err("seek out of bounds: newpos=%lld, limit=%d\n", (long long)newpos, PSEUDO_BUFFER_SIZE);
        mutex_unlock(&pseudo_mutex);
        return -EINVAL;
    }

    /* Apply new file offset position */
    file->f_pos = newpos;
    mutex_unlock(&pseudo_mutex);
    pr_info("seek to new pos=%lld (whence=%d, offset=%lld)\n", (long long)newpos, whence, (long long)offset);
    return newpos;
}

static int pseudo_open(struct inode *inode, struct file *file)
{
    pr_info("device opened successfully\n");
    return 0;
}

static int pseudo_release(struct inode *inode, struct file *file)
{
    pr_info("device closed successfully\n");
    return 0;
}

/* File operations structure binding system calls to driver hooks */
static const struct file_operations pseudo_fops = {
    .owner = THIS_MODULE,
    .open = pseudo_open,
    .release = pseudo_release,
    .read = pseudo_read,
    .write = pseudo_write,
    .llseek = pseudo_llseek,
};

/**
 * pseudo_init() - Driver module entry point.
 *
 * Performs standard dynamic device region allocation, character device
 * registration, and sysfs device node population.
 */
static int __init pseudo_init(void)
{
    struct device *dev;
    int ret;

    pr_info("initializing pseudo character driver...\n");

    /* 1. Allocate a dynamic major number */
    ret = alloc_chrdev_region(&pseudo_dev_number, 0, 1, "pseudodev");
    if (ret < 0) {
        pr_err("alloc_chrdev_region failed: %d\n", ret);
        return ret;
    }

    /* 2. Initialize and register the cdev structure */
    cdev_init(&pseudo_cdev, &pseudo_fops);
    pseudo_cdev.owner = THIS_MODULE;

    ret = cdev_add(&pseudo_cdev, pseudo_dev_number, 1);
    if (ret < 0) {
        pr_err("cdev_add failed: %d\n", ret);
        unregister_chrdev_region(pseudo_dev_number, 1);
        return ret;
    }

    /* 3. Create device class under /sys/class/ (signature has 1 argument in 6.4+) */
    pseudo_class = class_create("pseudo_class");
    if (IS_ERR(pseudo_class)) {
        ret = PTR_ERR(pseudo_class);
        pr_err("class_create failed: %d\n", ret);
        cdev_del(&pseudo_cdev);
        unregister_chrdev_region(pseudo_dev_number, 1);
        return ret;
    }

    /* 4. Create device node causing udev/devtmpfs to populate /dev/pseudodev */
    dev = device_create(pseudo_class, NULL, pseudo_dev_number, NULL, "pseudodev");
    if (IS_ERR(dev)) {
        ret = PTR_ERR(dev);
        pr_err("device_create failed: %d\n", ret);
        class_destroy(pseudo_class);
        cdev_del(&pseudo_cdev);
        unregister_chrdev_region(pseudo_dev_number, 1);
        return ret;
    }

    pr_info("registered major=%d minor=%d\n", MAJOR(pseudo_dev_number), MINOR(pseudo_dev_number));
    return 0;
}

/**
 * pseudo_exit() - Driver module cleanup point.
 *
 * Performs reverse-order teardown of sysfs class device node, cdev allocation,
 * and device number regions.
 */
static void __exit pseudo_exit(void)
{
    pr_info("cleaning up and unloading driver...\n");
    device_destroy(pseudo_class, pseudo_dev_number);
    class_destroy(pseudo_class);
    cdev_del(&pseudo_cdev);
    unregister_chrdev_region(pseudo_dev_number, 1);
    pr_info("unloaded\n");
}

module_init(pseudo_init);
module_exit(pseudo_exit);
