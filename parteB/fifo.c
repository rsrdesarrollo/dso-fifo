#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/vmalloc.h>
#include <asm-generic/uaccess.h>
#include <asm-generic/errno.h>
#include <linux/semaphore.h>
#include "cbuffer.h"
/*
 *  Lecturas o escritura de mas de BUF_LEN -> Error
 *  Al abrir un FIFO en lectura se BLOQUEA hasta habrir la escritura y viceversa
 *  El PRODUCTOR se BLOQUEA si no hay hueco
 *  El CONSUMIDOR se BLOQUEA si no tiene todo lo que pide
 *  Lectura a FIFO VACIO sin PRODUCTORES -> EOF 0
 *  Escritura a FIFO sin CONSUMIDOR -> Error
 */

int init_module(void);
void cleanup_module(void);
static int fifo_open(struct inode *, struct file *);
static int fifo_release(struct inode *, struct file *);
static ssize_t fifo_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t fifo_write(struct file * ,const char __user * ,size_t ,loff_t *);

#define DEVICE_NAME "fifodev"
#define BUF_LEN 512
#define FIFO_DEBUG

cbuffer_t* cbuffer;

int num_prod = 0;
int num_cons = 0;

int sleepy_friends = 0;
int num_bloq_prod = 0;
int num_bloq_cons = 0;


DEFINE_SEMAPHORE(mutex);
struct semaphore cola_prod, cola_cons, wait_friend;

#ifdef FIFO_DEBUG
    #define DBG(format, arg...) do { \
        printk(KERN_DEBUG "%s: " format "\n" , __func__ , ## arg); \
    } while (0)
#else
    #define DBG(format, arg...) /* */
#endif

#define cond_wait(mtx, cond, count, interrupt_handler) \
    do { \
        count++; \
        up(mtx); \
        DBG("Me voy a dormir en "#cond" con "#count": %d", count); \
        if (down_interruptible(cond)){ \
            DBG("[INT] Despertado de "#cond" por interrupción }:(");\
            down(mtx); \
            count--; \
            up(mtx); \
            interrupt_handler \
            return -EINTR; \
        } \
        if (down_interruptible(mtx)){ \
            interrupt_handler \
            return -EINTR; \
        }\
    } while(0)



#define __Handler__


static int Major;  

static struct file_operations fops = {
    .read = fifo_read,
    .write = fifo_write,
    .open = fifo_open,
    .release = fifo_release
};



int init_module(void)
{
    Major = register_chrdev(0, DEVICE_NAME, &fops);

    if (Major < 0) {
        printk(KERN_ALERT "Registering char device failed with %d\n", Major);
        return Major;
    }

    if((cbuffer = create_cbuffer_t(BUF_LEN)) == NULL)
        return -ENOMEM;

    sema_init(&cola_cons, 0);
    sema_init(&cola_prod, 0);
    sema_init(&wait_friend, 0);

    DBG("I was assigned major number %d. To talk to\n", Major);
    DBG("the driver, create a dev file with\n");
    DBG("'mknod /var/tmp/fifo -m 666 c %d 0'.\n", Major);
    DBG("Remove the device file and module when done.\n");

    return 0;
}

void cleanup_module(void)
{
    unregister_chrdev(Major, DEVICE_NAME);
    destroy_cbuffer_t(cbuffer);
}



static int fifo_open(struct inode *inode, struct file *file)
{
    char is_cons = file->f_mode & FMODE_READ;

    DBG("Pipe abierto para %s con lecotres %d, escriores %d", 
            (file->f_mode & FMODE_READ)? "lectura": "escritura",
            num_cons, 
            num_prod);

    // INICIO SECCIÓN CRÍTICA >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
    if (down_interruptible(&mutex)) 
        return -EINTR;

    if (is_cons) 
        num_cons++;
    else 
        num_prod++;


    // Si falta de algun tipo esperar a un amigo
    if(!(num_cons && num_prod)){
        cond_wait(&mutex, &wait_friend, sleepy_friends,
                __Handler__ { 
                    down(&mutex);
                    if(is_cons)
                        num_cons--;
                    else
                        num_prod--;
                    up(&mutex);
                });
    }

    // Si hay amigos esperando despierta a todos
    while(sleepy_friends){
        sleepy_friends--;
        up(&wait_friend);
    }

    up(&mutex);
        
    // FIN SECCIÓN CRÍTICA <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
    try_module_get(THIS_MODULE);

    return 0;
}


static int fifo_release(struct inode *inode, struct file *file)
{
    
    // INCIO SECCIÓN CRÍTICA >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
    if (down_interruptible(&mutex)) 
        return -EINTR;

    if (file->f_mode & FMODE_READ) 
        num_cons--;
    else 
        num_prod--;


    if( !(num_prod || num_cons) )
        cbuffer->size = 0;

    up(&mutex);
    // FIN SECCIÓN CRÍTICA <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
    
    DBG("Pipe de %s cerrado con lecotres %d, escriores %d", 
            (file->f_mode & FMODE_READ)? "lectura": "escritura",
            num_cons, 
            num_prod);

    module_put(THIS_MODULE);

    return 0;
}



static ssize_t fifo_read (struct file *filp,	
                            char __user *buff,	
                            size_t length,	
                            loff_t *offset)
{
    char *kbuff;
    DBG("Quiero leer %d bytes", length);
    DBG("Escritores esperando %d", num_bloq_prod);

    if (length > BUF_LEN){
        DBG("[ERROR] Lectura demasiado grande");
        return -EINVAL;
    }

    if((kbuff = vmalloc(length)) == NULL){
        DBG("[ERROR] no se ha podido alojar memoria dinámica");
        return -ENOMEM;
    }

    // INICIO SECCIÓN CRÍTICA >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
    if (down_interruptible(&mutex)){ 
        DBG("[INT] Interrumpido al intentar acceder a la SC");
        return -EINTR;
    }

    // Si el pipe esta vacio y no hay productores -> EOF
    if (num_prod == 0 && is_empty_cbuffer_t(cbuffer)){
        up(&mutex);
        DBG("Pipe vacio sin productores");
        return 0;
    }

    // El consumidor se bloquea si no tiene lo que pide
    while (size_cbuffer_t(cbuffer) < length){
        cond_wait(&mutex, &cola_cons, num_bloq_cons,
                __Handler__ {
                    vfree(kbuff);
                });
    }

    remove_items_cbuffer_t(cbuffer, kbuff, length); 
    
    // Broadcast a todos los productores, hay nuevos huecos
    while(num_bloq_prod){
        up(&cola_prod);
        num_bloq_prod--;
    }

    up(&mutex);
    // FIN SECCIÓN CRÍTICA <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

    length -= copy_to_user(buff, kbuff, length);
    
    DBG("[TERMINADO] escritores esperando %d", num_bloq_prod);

    vfree(kbuff);

    return length;
}


static ssize_t fifo_write (struct file *filp, 
                            const char __user *buff, 
                            size_t length, 
                            loff_t *offset)
{

    char *kbuff;

    DBG("Quiero escribir %d bytes", length);

    if (length > BUF_LEN){
        DBG("[ERROR] Demasiado para escribir");
        return -EINVAL;
    }

    if((kbuff = vmalloc(length)) == NULL){
        DBG("[ERROR] no se ha podido alojar memoria dinámica");
        return -ENOMEM;
    }

    length -= copy_from_user(kbuff, buff, length);

    // INICIO SECCIÓN CRÍTICA >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
    if (down_interruptible(&mutex)){
        DBG("[INT] Interrumpido al intentar acceder a la SC");
        return -EINTR;
    }

    // Si escribe sin consumiedores -> Error
    if (num_cons == 0){
        up(&mutex);
        DBG("[ERROR] Escritura sin consumidor");
        return -EPIPE;
    }

    // El productor se bloquea si no hay espacio
    while (nr_gaps_cbuffer_t(cbuffer) < length)
        cond_wait(&mutex, &cola_prod, num_bloq_prod,
                __Handler__ {
                    vfree(kbuff);
                });
    
    insert_items_cbuffer_t(cbuffer, kbuff, length); 
    
    // Broadcast a todos los consumidores, ya hay algo.
    while(num_bloq_cons){
        up(&cola_cons);
        num_bloq_cons--;
    }

    up(&mutex);
    // FIN SECCIÓN CRÍTICA <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
    
    DBG("[TERMINADO] lectores esperando %d", num_bloq_cons);

    vfree(kbuff);

    return length;
}
