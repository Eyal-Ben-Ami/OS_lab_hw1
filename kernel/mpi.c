#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <asm/uaccess.h>



asmlinkage int sys_mpi_register(int mpi_gid) {

    struct list_head* pos;
    struct mpi_group* current_group;
    struct mpi_group* new_group;

    //search if process is already registered to gid
    spin_lock(&current->mpi_lock);
    list_for_each(pos, &current->mpi_groups_list) {
        current_group = list_entry(pos, struct mpi_group, list);
        if (current_group->gid == mpi_gid) {
            spin_unlock(&current->mpi_lock);
            printk("debug: eyal: process %d is already registered to mpi_group %d\n", current->pid, mpi_gid);
            return 0;
        }
    }
    spin_unlock(&current->mpi_lock);

    //here the process is not already registered to gid
    //create new node
    new_group = kmalloc(sizeof(struct mpi_group), GFP_KERNEL);
    if (!new_group) {
        printk("debug: eyal: kmalloc failed in sys_mpi_register when trying to add %d to process %d\n", mpi_gid, current->pid);
        return -ENOMEM;
    }
    new_group->gid = mpi_gid;

    //add new node to the groups list
    spin_lock(&current->mpi_lock);
    list_add_tail(&new_group->list, &current->mpi_groups_list);
    spin_unlock(&current->mpi_lock);

    printk("debug: eyal: successfully added process %d to mpi_group %d\n", current->pid, mpi_gid);
    return 0;
}
