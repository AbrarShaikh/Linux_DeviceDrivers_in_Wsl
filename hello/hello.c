#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Developer");
MODULE_DESCRIPTION("A simple ARM64 Hello World LKM");
MODULE_VERSION("0.1");

static int __init hello_init(void) {
    pr_info("Hello, ARM64 world! LKM loaded successfully.\n");
    return 0;
}

static void __exit hello_exit(void) {
    pr_info("Goodbye, ARM64 world! LKM unloaded successfully.\n");
}

module_init(hello_init);
module_exit(hello_exit);
