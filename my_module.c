#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/list.h>
#include <linux/ioctl.h>


#define MEM_SIZE	2048
#define PS_BFS _IOW('a','a',int32_t*)
#define PS_DFS _IOW('a','b',int32_t*)
int received_pid = 0;

struct task_queue {
	struct task_struct *tasks[1024];
	int front;
	int rear;
	int size;
} tq;

void push_task(struct task_struct* task){
	tq.tasks[++tq.rear] = task;
  tq.size++;
	if(tq.rear == 1023)
		tq.rear = -1;
}

struct task_struct* pop_task(void){
	struct task_struct* task = tq.tasks[tq.front];
	tq.tasks[tq.front] = NULL;
	tq.front++;
	tq.size--;
	if(tq.front == 1023)
		tq.front = 0;
	return task;
}

dev_t dev = 0;

static struct class *dev_class;
static struct cdev my_cdev;

static int __init my_driver_init(void);
static void __exit my_driver_exit(void);
static long my_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

static struct file_operations fops = 
{
	.owner	= THIS_MODULE,
	.unlocked_ioctl = my_ioctl,
};

static int __init my_driver_init(void)
{
	// initializing the task queue
	tq.front = 0;
	tq.rear = -1;
	tq.size = 0;

	/* Allocating Major number dynamically*/
	/* &dev is address of the device */
	/* <0 to check if major number is created */
	if((alloc_chrdev_region(&dev, 0, 1, "my_Dev")) < 0) {
		printk(KERN_INFO"Cannot allocate the major number...\n");
	}

	printk(KERN_INFO"Major = %d Minor =  %d..\n", MAJOR(dev),MINOR(dev));

	/* creating cdev structure*/
	cdev_init(&my_cdev, &fops);

	/* adding character device to the system */
	if((cdev_add(&my_cdev, dev, 1)) < 0) {
		printk(KERN_INFO "Cannot add the device to the system...\n");
		goto r_class;
	}	 

	/* creating struct class */
	if((dev_class =  class_create(THIS_MODULE, "my_class")) == NULL) {
		printk(KERN_INFO " cannot create the struct class...\n");
		goto r_class;
	}

	/* creating device */

	if((device_create(dev_class, NULL, dev, NULL, "my_device")) == NULL) {
		printk(KERN_INFO " cannot create the device ..\n");
		goto r_device;
	}

	printk(KERN_INFO"Device driver insert...done properly...");
	return 0;

r_device: 
	class_destroy(dev_class);

r_class:
	unregister_chrdev_region(dev, 1);
	return -1;
}

void __exit my_driver_exit(void) {
	device_destroy(dev_class, dev);
	class_destroy(dev_class);
	cdev_del(&my_cdev);
	unregister_chrdev_region(dev, 1);
	printk(KERN_INFO "Device driver is removed successfully...\n");
}

void dfs_helper(struct task_struct *task){
	struct task_struct *child;
	struct list_head *task_list;

	printk(KERN_INFO "task: %s, pid: %d\n", task->comm, task->pid);

	list_for_each(task_list, &task->children){
		child = list_entry(task_list, struct task_struct, sibling);
		dfs_helper(child);
	}
}

void dfs(pid_t pid){
	struct task_struct *task;

	for_each_process(task){
		if (task->pid == pid){
			printk(KERN_INFO "DFS for the task: %s, pid: %d\n", task->comm, task->pid);
			dfs_helper(task);
		}
	}
	printk(KERN_INFO "End of DFS");
}

void bfs_helper(void){
	struct task_struct *child;
	struct task_struct *task;
	struct list_head *list;

	while(tq.size != 0){
		task = pop_task();
		
		list_for_each(list, &task->children){
			child= list_entry(list, struct task_struct, sibling);
			push_task(child);
			printk(KERN_INFO "task: %s, pid: %d\n", child->comm, child->pid);
		}
	}
}

void bfs(pid_t target_pid){
	struct task_struct *task;

	for_each_process(task){
		if (task->pid == target_pid){
				printk(KERN_INFO "BFS for the task: %s, pid: %d\n", task->comm, task->pid);
				push_task(task);
				bfs_helper();
		}
	}
	printk(KERN_INFO "End of BFS");
}

static long my_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	if (copy_from_user(&received_pid ,(int*) arg, sizeof(received_pid)))
		return -EFAULT;

	if (cmd == PS_BFS)
		bfs(received_pid);
	else if (cmd == PS_DFS)
		dfs(received_pid);
	else
		printk(KERN_INFO "Invalid command");

	return 0;
}

module_init(my_driver_init);
module_exit(my_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Onur Eren Arpaci, Alp Ozaslan");
MODULE_DESCRIPTION("Kernel Process Traverse");


