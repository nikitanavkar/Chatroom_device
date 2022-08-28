#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/kfifo.h>
#include <linux/ioctl.h>
#include <linux/mutex.h>
#include "ioctl_bots_dev.h"

// Operation successful
#define SUCCESS 		1

// Operation result not known or no data 
#define NOT_EXPLORED 	0

// Operation failed
#define FAILURE 		-1

// Maximum number of bots allowed in the chatroom
#define MAX_PROCESSES 	20

/* fifo size in elements (bytes) */
#define FIFO_SIZE 		128

// Bot is active
#define ACTIVE 			1

// Bot is inactive
#define INACTIVE 		0

/**
 * struct for storing required process data.
 */
typedef struct process_data {
	int id;
	int state;
	struct kfifo_rec_ptr_1 fifo;
	spinlock_t mylock;
} process_data;

// Array to store the data for maximum number of bots allowed in the chatroom
process_data processes[MAX_PROCESSES];

// Maximum number of bots joined so far
static int N = 0;

// Mutex to lock the critical section
struct mutex cs_mutex;

/*
 * Function:  my_ioctl
 * -----------------------------------
 *  kernel module function to perform input/output on the file under /dev
 * 
 *  *file: the file pointer
 *
 *  cmd: command id to perform the input/output operation
 * 
 *  arg: data pointer from the process to use as input or output buffer
 *
 *  returns: value to be returned to the calling program.
 */
static long int my_ioctl(struct file *file, unsigned cmd, unsigned long arg)
{
	int i, ret = NOT_EXPLORED;
	ProcessInfo *l_msg;
	l_msg = (ProcessInfo *)kmalloc(sizeof(ProcessInfo), GFP_USER);

	if(!l_msg) 
	{
		printk("my_ioctl command failed - kmalloc\n");
		return FAILURE;
	}

	switch(cmd)
	{
		case JOIN_CHATROOM:
			{
				if(copy_from_user(l_msg, (ProcessInfo *) arg, sizeof(ProcessInfo)))
				{
					printk("my_ioctl JOIN_CHATROOM command failed\n");
					ret = FAILURE;
					break;
				}
				printk("my_ioctl JOIN_CHATROOM called by %d name - %s\n", l_msg->id, l_msg->msg);
				mutex_lock(&cs_mutex);
				for(i = 0; i < N; i++)
				{
					
					if(processes[i].state == INACTIVE)
					{
						ret = kfifo_alloc(&processes[i].fifo, FIFO_SIZE, GFP_KERNEL);
						if (ret) 
						{
							printk("my_ioctl JOIN_CHATROOM process %d kfifo allocation failed\n", l_msg->id);
							ret = FAILURE;
							break;
						}
    					spin_lock_init(&processes[i].mylock);
						processes[i].id = l_msg->id;
						processes[i].state = ACTIVE;
						printk("my_ioctl JOIN_CHATROOM process %d joined at index %d\n", l_msg->id, i);
						ret = SUCCESS;
						break;
					}
				}
				if(ret == NOT_EXPLORED)
				{
					if(N < MAX_PROCESSES - 1)
					{
						ret = kfifo_alloc(&processes[N].fifo, FIFO_SIZE, GFP_KERNEL);
						if (ret) 
						{
							printk("my_ioctl JOIN_CHATROOM process %d kfifo allocation failed\n", l_msg->id);
							ret = FAILURE;
						}
						else
						{
							spin_lock_init(&processes[N].mylock);
							processes[N].id = l_msg->id;
							processes[N].state = ACTIVE;
							printk("my_ioctl JOIN_CHATROOM process %d joined at index %d\n", l_msg->id, N);
							N++;
							ret = SUCCESS;
						}
					}
					else
					{
						printk("my_ioctl JOIN_CHATROOM chatroom full for process %d\n", l_msg->id);
						ret = FAILURE;
					}
				}
				// If successfully joined inform this to other bots
				if(ret == SUCCESS)
				{
					for(i = 0; i < N; i++)
					{
						if(processes[i].id != l_msg->id && processes[i].state == ACTIVE)
						{
							kfifo_in_spinlocked(&processes[i].fifo, l_msg->msg, sizeof(l_msg->msg), &processes[i].mylock);
							printk("my_ioctl JOIN_CHATROOM message by %d written at index %d \n", l_msg->id, i);
						}
					}
				}
				mutex_unlock(&cs_mutex);
				break;
			}

		case LEAVE_CHATROOM:
			{
				if(copy_from_user(l_msg, (ProcessInfo *) arg, sizeof(ProcessInfo)))
				{
					printk("my_ioctl LEAVE_CHATROOM command failed\n");
					ret = FAILURE;
					break;
				}
				printk("my_ioctl LEAVE_CHATROOM called by %d with msg - %s\n", l_msg->id, l_msg->msg);
				mutex_lock(&cs_mutex);
				for(i = 0; i < N; i++)
				{
					if(processes[i].id == l_msg->id)
					{
						kfifo_free(&processes[i].fifo);
						processes[i].state = INACTIVE;
						printk("my_ioctl LEAVE_CHATROOM process %d unregistered at index %d\n", l_msg->id, i);
					}	
					else if(processes[i].state == ACTIVE)
					{
						kfifo_in_spinlocked(&processes[i].fifo, l_msg->msg, sizeof(l_msg->msg), &processes[i].mylock);
						printk("my_ioctl LEAVE_CHATROOM message by %d written at index %d \n", l_msg->id, i);
					}
				}
				mutex_unlock(&cs_mutex);
				ret = SUCCESS;
				break;
			}

		case WR_MESSAGE:
			{
				if(copy_from_user(l_msg, (ProcessInfo *) arg, sizeof(ProcessInfo)))
				{
					printk("my_ioctl WR_MESSAGE command failed\n");
					ret = FAILURE;
					break;
				}
				printk("my_ioctl WR_MESSAGE called by %d with msg - %s\n", l_msg->id, l_msg->msg);
				mutex_lock(&cs_mutex);
				for(i = 0; i < N; i++)
				{
					if(processes[i].id != l_msg->id && processes[i].state == ACTIVE)
					{
						kfifo_in_spinlocked(&processes[i].fifo, l_msg->msg, sizeof(l_msg->msg), &processes[i].mylock);
						printk("my_ioctl WR_MESSAGE message by %d written at index %d\n", l_msg->id, i);
					}
				}
				mutex_unlock(&cs_mutex);
				ret = SUCCESS;
				break;
			}

		case RD_MESSAGE:
			{
				if(copy_from_user(l_msg, (ProcessInfo *) arg, sizeof(ProcessInfo)))
				{
					printk("my_ioctl RD_MESSAGE command failed\n");
					ret = FAILURE;
					break;
				}
				mutex_lock(&cs_mutex);
				for(i = 0; i < N; i++)
				{
					if(processes[i].id == l_msg->id)
					{
						char buf[BUFF_LEN];
						ret = kfifo_out_spinlocked(&processes[i].fifo, buf, sizeof(buf), &processes[i].mylock);
						if(ret > NOT_EXPLORED)
						{
							buf[ret] = '\0';
							printk("my_ioctl RD_MESSAGE found message for %d at index = %d and msg - %s\n", l_msg->id, i, buf);
							copy_to_user(((ProcessInfo *) arg)->msg, buf, sizeof(buf));
							ret = sizeof(buf);
							break;
						}
					}
				} 
				mutex_unlock(&cs_mutex);
				break;
			}
		
		default:
				break;
	}

	kfree(l_msg);
	return ret;
}

/*
 * Function:  open
 * -----------------------------------
 *  kernel module function to open the file under /dev
 *
 *  *inode: the file's inode reference
 *
 *  *file: the file pointer
 *
 *  returns: value to be returned to the calling program.
 */
static int myopen(struct inode *inode, struct file *file)
{
	printk("myopen called in Chatroom module by process %d\n", get_current()->pid);
	return NOT_EXPLORED;
}

/*
 * Function:  myclose
 * -----------------------------------
 *  kernel module function to close the file under /dev.
 *
 *  *inodep: the file's inode reference
 *
 *  *filep: the file pointer
 *
 *  returns: value to be returned to the calling program.
 */
static int myclose(struct inode *inodep, struct file *filp) {
	printk("myclose called in Chatroom module by process %d\n", get_current()->pid);
	return NOT_EXPLORED;
}

/*
 * Function:  mywrite
 * -----------------------------------
 *  kernel module function to write to the driver file to pass it to the hardware driver.
 *
 *  *file: the file pointer
 *
 *  *buf: the buffer which can be written to the file
 *
 *  *ppos: the offset
 *
 *  returns: value to be returned to the calling program.
 */
static ssize_t mywrite(struct file *file, const char __user *buf, size_t len, loff_t *ppos)
{
	printk("mywrite called in Chatroom module by process %d\n", get_current()->pid);
	return NOT_EXPLORED; 
}

/*
 * Function:  myread
 * -----------------------------------
 *  kernel module function to read from the driver file to pass it to the calling process.
 *
 *  *file: the file pointer
 *
 *  *buf: the buffer in which data can be written from the driver file
 *
 *  *ppos: the offset
 *
 *  returns: value to be returned to the calling program.
 */
static ssize_t myread(struct file *file, char __user *buf, size_t len, loff_t *ppos)
{
	printk("myread called in Chatroom module by process %d\n", get_current()->pid);
	return NOT_EXPLORED; 
}

/**
 * struct to define the operation to be performed on that particular file with their method names.
 */
static const struct file_operations myfops = {
    .owner					= THIS_MODULE,
    .read					= myread,
    .write					= mywrite,
    .open					= myopen,
    .release				= myclose,
    .llseek 				= no_llseek,
	.unlocked_ioctl 		= my_ioctl,
};

/**
 * struct to define the device drivers attributes such as name, minor number, etc
 */
struct miscdevice mydevice = 
{
    .minor = MISC_DYNAMIC_MINOR,
    .name = "akash_chatroom",
    .fops = &myfops,
    .mode = S_IRUGO | S_IWUGO,
};

/*
 * Function:  misc_init
 * -----------------------------------
 *  kernel module function to initialize the misc device driver using miscdevice struct
 *
 *  returns: return 0 if successfully loaded or error code.
 */
static int __init my_init(void)
{
	printk("my_init called\n");

	// register the character device
	if (misc_register(&mydevice) != NOT_EXPLORED) 
	{
		printk("device registration failed\n");
		return FAILURE;
	}

	mutex_init(&cs_mutex);

	printk("character device registered\n");
	return NOT_EXPLORED;
}

/*
 * Function:  misc_exit
 * -----------------------------------
 *  kernel module function to deregister the device driver using the struct reference.
 */
static void __exit my_exit(void)
{
	printk("my_exit called\n");
	misc_deregister(&mydevice);
}

module_init(my_init)
module_exit(my_exit)
MODULE_DESCRIPTION("Miscellaneous character device module for Chatroom\n");
MODULE_AUTHOR("Akash Jaiswal <ajaiswa3@binghamton.edu>");
MODULE_LICENSE("GPL");