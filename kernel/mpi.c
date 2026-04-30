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

asmlinkage int sys_mpi_send(pid_t pid, char* message, ssize_t message_size) {

    struct task_struct* receiver_task;
    struct list_head *pos1, *pos2;
    struct mpi_group *sender_group, *receiver_group;
    struct mpi_message *new_message;
    char *kernel_buffer;
    int shared_group = -1;

    //check the message is not empty
    if (message_size < 1 || !message) {
        printk("debug: eyal: message sent from process %d to process %d is empty\n", current->pid, pid);
        return -EINVAL;
    }

    //get the task_struct of the destination process and verify it exists
    receiver_task = find_task_by_pid(pid);
    if (!receiver_task) {
        printk("debug: eyal: message sent from process %d to not existing process %d\n", current->pid, pid);
        return -ESRCH;
    }

    //verify the processes share an MPI group
    spin_lock(&current->mpi_lock);
    spin_lock(&receiver_task->mpi_lock);

    list_for_each(pos1, &current->mpi_groups_list) {
        sender_group = list_entry(pos1, struct mpi_group, list);
        list_for_each(pos2, &receiver_task->mpi_groups_list) {
            receiver_group = list_entry(pos2, struct mpi_group, list);
            if (sender_group->gid == receiver_group->gid) {
                shared_group = sender_group->gid;
                break;
            }
        }
        if (shared_group != -1) {
            break;
        }
    }

    spin_unlock(&receiver_task->mpi_lock);
    spin_unlock(&current->mpi_lock);

    if (shared_group == -1) {
        printk("debug: eyal: message sent from process %d to process %d but they dont share an MPI group\n", current->pid, pid);
        return -EPERM;
    }

    //copy message to receiver message list
    //allocate kernel space for the message and the node
    kernel_buffer = kmalloc(message_size, GFP_KERNEL);
    if (!kernel_buffer) {
        printk("debug: eyal: message sent from process %d to process %d but kmalloc failed\n", current->pid, pid);
        return -ENOMEM;
    }

    new_message = kmalloc(sizeof(struct mpi_message), GFP_KERNEL);
    if (!new_message) {
        printk("debug: eyal: message sent from process %d to process %d but kmalloc failed\n", current->pid, pid);
        kfree(kernel_buffer);
        return -ENOMEM;
    }

    //copy message from user space to kernel space
    if (copy_from_user(kernel_buffer, message, message_size)) { //copy_from_user returns the number of bytes failed to copy, so 0 is success
        printk("debug: eyal: message sent from process %d to process %d but copy_from_user failed\n", current->pid, pid);
        kfree(kernel_buffer);
        kfree(new_message);
        return -EFAULT;
    }

    //put the message data in the node
    new_message->sender_pid = current->pid;
    new_message->gid = shared_group;
    new_message->data = kernel_buffer;
    new_message->size = message_size;

    //add new node to the receiver messages list
    spin_lock(&receiver_task->mpi_lock);
    list_add_tail(&new_message->list, &receiver_task->mpi_messages_list);
    spin_unlock(&receiver_task->mpi_lock);
    return 0;
}

asmlinkage int sys_mpi_receive(pid_t pid, char* message, ssize_t message_size) {

    struct list_head *pos;
    struct mpi_message *current_message;
    int found = 0;
    int num_of_bytes_read;

    if (!message || message_size < 1) {
        printk("debug: eyal: process %d tried to read a message from process %d but got bad parameters\n", current->pid, pid);
        return -EINVAL;
    }

    if (list_empty(&current->mpi_groups_list)) {
        printk("debug: eyal: process %d tried to read a message from process %d but it is not registered for MPI communication\n", current->pid, pid);
        return -EPERM;
    }

    //search a message from pid
    spin_lock(&current->mpi_lock);
    list_for_each(pos, &current->mpi_messages_list) {
        current_message = list_entry(pos, struct mpi_message, list);
        if (current_message->sender_pid == pid) {
            found = 1;
            list_del(&current_message->list); //remove this node from the list
            break;
        }
    }
    spin_unlock(&current->mpi_lock);

    if (!found) {
        printk("debug: eyal: process %d tried to read a message from process %d but no message from this process is currently in queue\n", current->pid, pid);
        return -EAGAIN;
    }

    //copy and delete the message
    if (current_message->size < message_size) {//make sure we don't try to read too much
        message_size = current_message->size;
    }
    num_of_bytes_read = message_size - copy_to_user(message, current_message->data, message_size);

    //release memory
    kfree(current_message->data);
    kfree(current_message);

    //check return value
    if (num_of_bytes_read == 0) {
        printk("debug: eyal: process %d tried to read a message from process %d but failed to write into user buffer\n", current->pid, pid);
        return -EFAULT;
    }

    return num_of_bytes_read;

}