#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/vmalloc.h>
#include <asm-generic/uaccess.h>
#include <asm-generic/errno.h>
#include <linux/semaphore.h>
#include "cbuffer.h"


#define MAX_ITEMS_CBUF	5
#define MAX_CHARS_KBUF	10

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Productor/Consumidor v1 para DSO");
MODULE_AUTHOR("Juan Carlos Sáez");

static struct proc_dir_entry *proc_entry;
static cbuffer_t* cbuf; /* Buffer circular compartido */
struct semaphore elementos,huecos; /* Semaforos para productor y consumidor */
struct semaphore mtx; /* Para garantizar exclusión mutua en acceso a buffer */

int prodcons_write( struct file *punterofichero, const char __user *bufferusuario,
                        unsigned long longitud, void *data )
{

  char kbuf[MAX_CHARS_KBUF+1];
  int val=0;
  int* item=NULL;

  if (longitud > MAX_CHARS_KBUF) {
    return -ENOSPC;
  }
  if (copy_from_user( kbuf, bufferusuario, longitud )) {
    return -EFAULT;
  }

  kbuf[longitud] ='\0'; 
  
  if (sscanf(kbuf,"%i",&val)!=1)
  {
	return -EINVAL;
  }
	
  item=vmalloc(sizeof(int));

  (*item)=val;

  /* Bloqueo hasta que haya huecos */  
  if (down_interruptible(&huecos))
  {
	vfree(item);
	return -EINTR;
  }

  /* Entrar a la SC */                              
  if (down_interruptible(&mtx))
  {
        up(&huecos);      
        return -EINTR;
  }

  /* Inserción segura en el buffer circular */
  insert_cbuffer_t(cbuf,item);

  /* Salir de la SC */
  up(&mtx);
 
  /* Incremento del número de elementos (reflejado en el semáforo) */
  up(&elementos);
  
  return longitud;
}


int prodcons_read( char *buffer, char **bufferlocation, off_t offset,
                   int buffer_lenghth, int *eof, void *data )

{

  int len=0;
  int* item=NULL;

  if (offset>0)
	return 0;

  /* Bloqueo hasta que haya elementos que consumir */  
  if (down_interruptible(&elementos))
  {
	return -EINTR;
  }

  /* Entrar a la SC  */  
  if (down_interruptible(&mtx))
  {
 	up(&elementos);	
	return -EINTR;
  }

  /* Obtener el primer elemento del buffer y eliminarlo */
  item=head_cbuffer_t(cbuf);
  remove_cbuffer_t(cbuf);  

  /* Salir de la SC */ 
  up(&mtx);
 
  /* Incremento del número de huecos (reflejado en el semáforo) */
  up(&huecos);
   
  len=sprintf(buffer,"%i\n",*item); 
  
  /* Liberar memoria del elemento extraido */
  vfree(item);
   
  return len;
}


int init_prodcons_module( void )
{

  int ret = 0;

  /* Inicialización del buffer */  
  cbuf = create_cbuffer_t(MAX_ITEMS_CBUF);

  /* Semaforo elementos inicializado a 0 (buffer vacío) */
  sema_init(&elementos,0); 

  /* Semaforo huecos inicializado a MAX_ITEMS_CBUF (buffer vacío) */
  sema_init(&huecos,MAX_ITEMS_CBUF);

  /* Semaforo para garantizar exclusion mutua */
  sema_init(&mtx,1);
  
  if (!cbuf) {
    ret = -ENOMEM;
  } else {

    proc_entry = create_proc_entry("prodcons",0666, NULL);
    if (proc_entry == NULL) {
      ret = -ENOMEM;
      destroy_cbuffer_t(cbuf);
      printk(KERN_INFO "Prodcons1: No puedo crear la entrada en proc\n");
    } else {
      proc_entry->read_proc = prodcons_read;
      proc_entry->write_proc = prodcons_write;
      printk(KERN_INFO "Prodcons1: Cargado el Modulo.\n");
    }
  }

  return ret;

}


void exit_prodcons_module( void )
{
  remove_proc_entry("prodcons", NULL);
  destroy_cbuffer_t(cbuf);
  printk(KERN_INFO "Prodcons1: Modulo descargado.\n");
}


module_init( init_prodcons_module );
module_exit( exit_prodcons_module );
