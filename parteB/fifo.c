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
//#define DEBUG_VERBOSE

cbuffer_t* cbuffer;

int num_prod = 0;
int num_cons = 0;

int num_bloq_prod = 0;
int num_bloq_cons = 0;


DEFINE_SEMAPHORE(mutex);
struct semaphore cola_prod, cola_cons;

#ifdef FIFO_DEBUG
    #define DBG(format, arg...) do { \
        printk(KERN_DEBUG "%s: " format "\n" , __func__ , ## arg); \
    } while (0)

    #ifdef DEBUG_VERBOSE
        #define DBGV DBG
    #else
        #define DBGV(format, args...) /* */
    #endif
#else
    #define DBG(format, arg...) /* */
    #define DBGV(format, args...) /* */
#endif

#define cond_wait(mtx, cond, count, interrupt_InterruptHandler) \
    do { \
        count++; \
        up(mtx); \
        DBGV("Me voy a dormir en "#cond" con "#count": %d", count); \
        if (down_interruptible(cond)){ \
            DBGV("[INT] Despertado de "#cond" por interrupción }:(");\
            down(mtx); \
            count--; \
            up(mtx); \
            interrupt_InterruptHandler \
            return -EINTR; \
        } \
        DBGV("Me han despertado de "#cond);\
        if (down_interruptible(mtx)){ \
            interrupt_InterruptHandler \
            return -EINTR; \
        }\
    } while(0)

#define cond_signal(cond) up(cond)

#define __InterruptHandler__


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

    DBGV("Pipe abierto para %s con lecotres %d, escriores %d", 
            (file->f_mode & FMODE_READ)? "lectura": "escritura",
            num_cons, 
            num_prod);

    // INICIO SECCIÓN CRÍTICA >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
    if (down_interruptible(&mutex)) 
        return -EINTR;

    if (is_cons){ 
        // Eres consumidor
        num_cons++;
        while(num_bloq_prod){
            num_bloq_prod--;
            up(&cola_prod);
        }
        
        while(!num_prod)
            cond_wait(&mutex, &cola_cons, num_bloq_cons,
                __InterruptHandler__ { 
                    down(&mutex);
                    num_cons--;
                    up(&mutex);
                }
            );

    }else{ 
        // Eres un productor.
        num_prod++;
        while(num_bloq_cons){
            num_bloq_cons--;
            up(&cola_cons);
	}
        
        while(!num_cons)
            cond_wait(&mutex, &cola_prod, num_bloq_prod,
                __InterruptHandler__ { 
                    down(&mutex);
                    num_prod--;
                    up(&mutex);
                }
            );
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

    if (file->f_mode & FMODE_READ){
        num_cons--;
	if(num_cons == 0) // Por si hay productores durmiendo, levántalos. 
            while(num_bloq_prod){
	        num_bloq_prod--;
                cond_signal(&cola_prod);
	    }
    }else 
        num_prod--;


    if( !(num_prod || num_cons) )
        cbuffer->size = 0;

    up(&mutex);
    // FIN SECCIÓN CRÍTICA <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
    
    DBGV("Pipe de %s cerrado con lecotres %d, escriores %d", 
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
    DBGV("Quiero leer %d bytes", length);
    DBGV("Escritores esperando %d", num_bloq_prod);

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
        DBGV("[INT] Interrumpido al intentar acceder a la SC");
        vfree(kbuff);
        return -EINTR;
    }

    // Si el pipe esta vacio y no hay productores -> EOF
    if (num_prod == 0 && is_empty_cbuffer_t(cbuffer)){
        up(&mutex);
        DBGV("Pipe vacio sin productores");
        vfree(kbuff);
        return 0;
    }

    // El consumidor se bloquea si no tiene lo que pide
    while (size_cbuffer_t(cbuffer) < length){
        cond_wait(&mutex, &cola_cons, num_bloq_cons,
                __InterruptHandler__ {
                    vfree(kbuff);
                });
    
        if (num_prod == 0 && is_empty_cbuffer_t(cbuffer)){
            up(&mutex);
	    DBGV("Pipe vacio sin productores");
    	    vfree(kbuff);
	    return 0;
        }
    }

    remove_items_cbuffer_t(cbuffer, kbuff, length); 
    
    // Broadcast a todos los productores, hay nuevos huecos
    while(num_bloq_prod){
        cond_signal(&cola_prod);
        num_bloq_prod--;
    }

    up(&mutex);
    // FIN SECCIÓN CRÍTICA <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<

    length -= copy_to_user(buff, kbuff, length);
    
    DBGV("[TERMINADO] escritores esperando %d", num_bloq_prod);

    vfree(kbuff);
    return length;
}


static ssize_t fifo_write (struct file *filp, 
                            const char __user *buff, 
                            size_t length, 
                            loff_t *offset)
{

    char *kbuff;

    DBGV("Quiero escribir %d bytes", length);

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
        DBGV("[INT] Interrumpido al intentar acceder a la SC");
        vfree(kbuff);
        return -EINTR;
    }

    // Si escribe sin consumiedores -> Error
    if (num_cons == 0){
        up(&mutex);
        DBG("[ERROR] Escritura sin consumidor");
        vfree(kbuff);
        return -EPIPE;
    }

    // El productor se bloquea si no hay espacio
    while (num_cons > 0 && nr_gaps_cbuffer_t(cbuffer) < length)
        cond_wait(&mutex, &cola_prod, num_bloq_prod,
                __InterruptHandler__ {
                    vfree(kbuff);
                });

    // Comprobamos que al salir de un posible wait sigue habiendo consumidores.
    if (num_cons == 0){
        up(&mutex);
	DBG("[ERROR] Escritura sin consumidor.");
	vfree(kbuff);
	return -EPIPE;
    }
    
    insert_items_cbuffer_t(cbuffer, kbuff, length); 
    
    // Broadcast a todos los consumidores, ya hay algo.
    while(num_bloq_cons){
        cond_signal(&cola_cons);
        num_bloq_cons--;
    }

    up(&mutex);
    // FIN SECCIÓN CRÍTICA <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
    
    DBGV("[TERMINADO] lectores esperando %d", num_bloq_cons);

    vfree(kbuff);
    return length;
}
