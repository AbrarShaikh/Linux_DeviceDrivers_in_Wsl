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
#define pr_fmt(fmt) "%s: " fmt, __func__

/* Device configuration */
#define MAX_DEVICES 4
#define PSEUDO_BUFFER_SIZE 4096

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Developer");
MODULE_DESCRIPTION("Multi-device pseudo character driver");
MODULE_VERSION("0.1");

/* Per-device state structure */
struct pseudo_device_data {
    char buffer[PSEUDO_BUFFER_SIZE];  /* Storage for device data */
    size_t data_size;                 /* Current size of valid data written */
    struct mutex mutex;               /* Mutex to serialize access to this device */
    struct device *device;            /* Device pointer for sysfs representation */
    int minor;                        /* Minor number for this device */
};

/* Global driver state */
static dev_t pseudo_dev_number;                         /* Device major/minor number start */
static struct cdev pseudo_cdev;                         /* Single cdev structure */
static struct class *pseudo_class;                      /* Shared device class */
static struct pseudo_device_data devices[MAX_DEVICES];  /* Array of device instances */

/**
 * pseudo_open() - Open a specific pseudo device.
 */
static int pseudo_open(struct inode *inode, struct file *file)
{
    unsigned int minor = iminor(inode);
    struct pseudo_device_data *dev_data;

    pr_info("Attempting to open minor %u\n", minor);

    if (minor >= MAX_DEVICES) {
        pr_err("invalid minor number %u\n", minor);
        return -ENODEV;
    }

    dev_data = &devices[minor];
    file->private_data = dev_data; /* Store pointer to device state for other operations */

    pr_info("[Device %d] opened successfully\n", dev_data->minor);
    return 0;
}

/**
 * pseudo_release() - Close/release the pseudo device.
 */
static int pseudo_release(struct inode *inode, struct file *file)
{
    struct pseudo_device_data *dev_data = file->private_data;

    pr_info("[Device %d] closed successfully\n", dev_data->minor);
    return 0;
}

/**
 * pseudo_read() - Read data from a specific pseudo device buffer.
 */
static ssize_t pseudo_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    struct pseudo_device_data *dev_data = file->private_data;
    size_t available;
    size_t to_copy;

    if (!dev_data)
        return -EINVAL;

    /* Acquire mutex for the specific device to prevent race conditions */
    if (mutex_lock_interruptible(&dev_data->mutex))
        return -ERESTARTSYS;

    /* Ensure current position offset is valid */
    if (*ppos < 0) {
        pr_err("[Device %d] negative offset %lld\n", dev_data->minor, (long long)*ppos);
        mutex_unlock(&dev_data->mutex);
        return -EINVAL;
    }

    /* Return EOF (0) if position is past logical EOF */
    if (*ppos >= dev_data->data_size) {
        pr_info("[Device %d] EOF reached (pos=%lld, data_size=%zu)\n", dev_data->minor, (long long)*ppos, dev_data->data_size);
        mutex_unlock(&dev_data->mutex);
        return 0;
    }

    /* Calculate bytes remaining from current position to logical data end in unsigned space */
    available = dev_data->data_size - *ppos;
    to_copy = min(count, available);

    /*
     * NOTE on Robustness/DoS Consideration:
     * Holding the dev_data->mutex across copy_to_user() means a slow or malicious
     * userspace buffer (e.g. backed by userfaultfd or blocking on network storage)
     * can stall inside the copy, blocking other openers of the same device indefinitely.
     * For a simple learning driver, we hold the mutex directly, but a production-grade
     * implementation would drop the lock before copying or use a bounce buffer.
     */
    if (copy_to_user(buf, dev_data->buffer + *ppos, to_copy)) {
        pr_err("[Device %d] copy_to_user failed for pos=%lld, count=%zu\n", dev_data->minor, (long long)*ppos, to_copy);
        mutex_unlock(&dev_data->mutex);
        return -EFAULT;
    }

    /* Advance the position and release the lock */
    *ppos += to_copy;
    mutex_unlock(&dev_data->mutex);

    pr_info("[Device %d] read %zu bytes, new pos=%lld, data_size=%zu\n",
            dev_data->minor, to_copy, (long long)*ppos, dev_data->data_size);
    return (ssize_t)to_copy;
}

/**
 * pseudo_write() - Write data to a specific pseudo device buffer.
 */
static ssize_t pseudo_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    struct pseudo_device_data *dev_data = file->private_data;
    size_t max_write;
    size_t to_copy;

    if (!dev_data)
        return -EINVAL;

    /* Acquire mutex for the specific device */
    if (mutex_lock_interruptible(&dev_data->mutex))
        return -ERESTARTSYS;

    /* Ensure offset is not negative */
    if (*ppos < 0) {
        pr_err("[Device %d] negative offset %lld\n", dev_data->minor, (long long)*ppos);
        mutex_unlock(&dev_data->mutex);
        return -EINVAL;
    }

    /* If write starts at or after physical buffer limit, return No Space */
    if (*ppos >= PSEUDO_BUFFER_SIZE) {
        pr_warn("[Device %d] write attempt past buffer limit (pos=%lld, limit=%d)\n", dev_data->minor, (long long)*ppos, PSEUDO_BUFFER_SIZE);
        mutex_unlock(&dev_data->mutex);
        return -ENOSPC;
    }

    /* Clamp write size to the physical size of the buffer in unsigned space */
    max_write = PSEUDO_BUFFER_SIZE - *ppos;
    to_copy = min(count, max_write);

    /*
     * NOTE on Robustness/DoS Consideration:
     * Holding the dev_data->mutex across copy_from_user() means a slow or malicious
     * userspace buffer can stall inside the copy, blocking other openers of the same
     * device indefinitely. For simplicity in this learning driver, we hold the mutex,
     * but a production implementation would avoid holding the lock during copy.
     */
    if (copy_from_user(dev_data->buffer + *ppos, buf, to_copy)) {
        pr_err("[Device %d] copy_from_user failed for pos=%lld, count=%zu\n", dev_data->minor, (long long)*ppos, to_copy);
        mutex_unlock(&dev_data->mutex);
        return -EFAULT;
    }

    /* Update offset and update logical EOF tracking if expanded */
    *ppos += to_copy;

    /* 
     * NOTE: Sparse-write behavior:
     * Seeking past data_size and writing causes data_size to jump forward,
     * leaving a zero-filled gap in between. This is intentional.
     */
    dev_data->data_size = max(dev_data->data_size, (size_t)*ppos);

    mutex_unlock(&dev_data->mutex);

    pr_info("[Device %d] wrote %zu bytes, new pos=%lld, data_size=%zu\n",
            dev_data->minor, to_copy, (long long)*ppos, dev_data->data_size);
    return (ssize_t)to_copy;
}

/**
 * pseudo_llseek() - Reposition file offset for a specific pseudo device.
 */
static loff_t pseudo_llseek(struct file *file, loff_t offset, int whence)
{
    struct pseudo_device_data *dev_data = file->private_data;
    loff_t newpos;

    if (!dev_data)
        return -EINVAL;

    switch (whence) {
    case SEEK_SET:
        newpos = offset;
        break;
    case SEEK_CUR:
        newpos = file->f_pos + offset;
        break;
    case SEEK_END:
        /*
         * f_pos is per-open file state, not shared device state. The lock
         * is only needed here to safely read dev_data->data_size without
         * racing with concurrent writes that modify it.
         */
        if (mutex_lock_interruptible(&dev_data->mutex))
            return -ERESTARTSYS;
        newpos = dev_data->data_size + offset;
        mutex_unlock(&dev_data->mutex);
        break;
    default:
        pr_err("[Device %d] invalid whence=%d\n", dev_data->minor, whence);
        return -EINVAL;
    }

    /* Verify new position is within buffer range [0, PSEUDO_BUFFER_SIZE] */
    if (newpos < 0 || newpos > PSEUDO_BUFFER_SIZE) {
        pr_err("[Device %d] seek out of bounds: newpos=%lld, limit=%d\n", dev_data->minor, (long long)newpos, PSEUDO_BUFFER_SIZE);
        return -EINVAL;
    }

    file->f_pos = newpos;

    pr_info("[Device %d] seek to new pos=%lld (whence=%d, offset=%lld)\n",
            dev_data->minor, (long long)newpos, whence, (long long)offset);
    return newpos;
}


/* File operations binding */
static const struct file_operations pseudo_fops = {
    .owner = THIS_MODULE,
    .open = pseudo_open,
    .release = pseudo_release,
    .read = pseudo_read,
    .write = pseudo_write,
    .llseek = pseudo_llseek,
};

/**
 * pseudo_init() - Module entry point.
 */
static int __init pseudo_init(void)
{
    int ret;
    int i;

    pr_info("initializing multi-device pseudo character driver...\n");

    /* 1. Allocate dynamic major number for the maximum number of devices */
    ret = alloc_chrdev_region(&pseudo_dev_number, 0, MAX_DEVICES, "pseudodev_multi");
    if (ret < 0) {
        pr_err("alloc_chrdev_region failed: %d\n", ret);
        return ret;
    }

    /* 2. Initialize and register the cdev structure for MAX_DEVICES */
    cdev_init(&pseudo_cdev, &pseudo_fops);
    pseudo_cdev.owner = THIS_MODULE;

    ret = cdev_add(&pseudo_cdev, pseudo_dev_number, MAX_DEVICES);
    if (ret < 0) {
        pr_err("cdev_add failed: %d\n", ret);
        unregister_chrdev_region(pseudo_dev_number, MAX_DEVICES);
        return ret;
    }

    /* 3. Create device class under /sys/class/ (1-arg signature for modern kernel versions) */
    pseudo_class = class_create("pseudo_multi_class");
    if (IS_ERR(pseudo_class)) {
        ret = PTR_ERR(pseudo_class);
        pr_err("class_create failed: %d\n", ret);
        cdev_del(&pseudo_cdev);
        unregister_chrdev_region(pseudo_dev_number, MAX_DEVICES);
        return ret;
    }

    /* 4. Initialize device instances and register device nodes under /dev/ */
    for (i = 0; i < MAX_DEVICES; i++) {
        devices[i].minor = i;
        devices[i].data_size = 0;
        mutex_init(&devices[i].mutex);
        /* Note: devices[i].buffer is in BSS (statically allocated) and thus pre-zeroed */

        /* Create device node e.g. /dev/pseudodev0, /dev/pseudodev1, etc. */
        devices[i].device = device_create(pseudo_class, NULL,
                                          MKDEV(MAJOR(pseudo_dev_number), i),
                                          NULL, "pseudodev%d", i);
        if (IS_ERR(devices[i].device)) {
            ret = PTR_ERR(devices[i].device);
            pr_err("device_create failed for minor %d: %d\n", i, ret);

            mutex_destroy(&devices[i].mutex);
            /* Clean up already registered devices in reverse order */
            while (--i >= 0) {
                device_destroy(pseudo_class, MKDEV(MAJOR(pseudo_dev_number), i));
                mutex_destroy(&devices[i].mutex);
            }
            class_destroy(pseudo_class);
            cdev_del(&pseudo_cdev);
            unregister_chrdev_region(pseudo_dev_number, MAX_DEVICES);
            return ret;
        }
    }

    pr_info("registered driver successfully with %d devices starting at major=%d\n",
            MAX_DEVICES, MAJOR(pseudo_dev_number));
    return 0;
}

/**
 * pseudo_exit() - Module exit point.
 */
static void __exit pseudo_exit(void)
{
    int i;

    pr_info("cleaning up and unloading multi-device driver...\n");

    /* Destroy device nodes under /sys/class/ and /dev/ and clean up mutexes */
    for (i = 0; i < MAX_DEVICES; i++) {
        device_destroy(pseudo_class, MKDEV(MAJOR(pseudo_dev_number), i));
        mutex_destroy(&devices[i].mutex);
    }

    /* Destroy class, delete cdev, and unregister device numbers */
    class_destroy(pseudo_class);
    cdev_del(&pseudo_cdev);
    unregister_chrdev_region(pseudo_dev_number, MAX_DEVICES);

    pr_info("unloaded successfully\n");
}

module_init(pseudo_init);
module_exit(pseudo_exit);
