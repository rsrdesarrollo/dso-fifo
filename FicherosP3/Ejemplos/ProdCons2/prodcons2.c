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
MODULE_DESCRIPTION("Productor/Consumidor v2 para DSO");
MODULE_AUTHOR("Juan Carlos Sáez");

static struct proc_dir_entry *proc_entry;
static cbuffer_t* cbuf;
struct semaphore prod_queue,cons_queue;
struct semaphore mtx;
int nr_prod_waiting,nr_cons_waiting;

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

  /* Acceso a la sección crítica */
  if (down_interruptible(&mtx))
  {
	return -EINTR;
  }

  /* Bloquearse mientras no haya huecos en el buffer */
  while (is_full_cbuffer_t(cbuf))
  {
	/* Incremento de productores esperando */
	nr_prod_waiting++;

	/* Liberar el 'mutex' antes de bloqueo*/
	up(&mtx);

	/* Bloqueo en cola de espera */		
	if (down_interruptible(&prod_queue)){
		down(&mtx);
		nr_prod_waiting--;
		up(&mtx);		
		return -EINTR;
	}	

	/* Readquisición del 'mutex' antes de entrar a la SC */				
	if (down_interruptible(&mtx)){
		return -EINTR;
	}	
  }

  /* Insertar en el buffer */
  insert_cbuffer_t(cbuf,item); 
  
  /* Despertar a los productores bloqueados (si hay alguno) */
  if (nr_cons_waiting>0)
  {
	up(&cons_queue);	
	nr_cons_waiting--;
  }

  /* Salir de la sección crítica */
  up(&mtx);
  
  return longitud;
}


int prodcons_read( char *buffer, char **bufferlocation, off_t offset,
                   int buffer_lenghth, int *eof, void *data )

{

  int len=0;
  int* item=NULL;

  if (offset>0)
	return 0;

  /* Entrar a la sección crítica */
  if (down_interruptible(&mtx))
  {
	return -EINTR;
  }

 /* Bloquearse mientras buffer esté vacío */
  while (size_cbuffer_t(cbuf)==0)
  {
	/* Incremento de consumidores esperando */
	nr_cons_waiting++;

	/* Liberar el 'mutex' antes de bloqueo*/
	up(&mtx);
	
	/* Bloqueo en cola de espera */		
	if (down_interruptible(&cons_queue)){
		down(&mtx);
		nr_cons_waiting--;
		up(&mtx);		
		return -EINTR;
	}	
	
	/* Readquisición del 'mutex' antes de entrar a la SC */		
	if (down_interruptible(&mtx)){
		return -EINTR;
	}	
  }

  /* Obtener el primer elemento del buffer y eliminarlo */
  item=head_cbuffer_t(cbuf);
  remove_cbuffer_t(cbuf);  
  
  /* Despertar a los consumidores bloqueados (si hay alguno) */
  if (nr_prod_waiting>0)
  {
	up(&prod_queue);	
	nr_prod_waiting--;
  }

  /* Salir de la sección crítica */	
  up(&mtx);
   
  len=sprintf(buffer,"%i\n",*item); 
  
  /* Liberar memoria del elemento extraido */
  vfree(item);
   
  return len;
}


int init_prodcons_module( void )
{

  int ret = 0;
  
  /* Inicialización del buffer circular */
  cbuf = create_cbuffer_t(MAX_ITEMS_CBUF);

  /* Inicialización a 0 de los semáforos usados como colas de espera */
  sema_init(&prod_queue,0);
  sema_init(&cons_queue,0);

  /* Inicializacion a 1 del semáforo que permite acceso en exclusión mutua a la SC */
  sema_init(&mtx,1);

  nr_prod_waiting=nr_cons_waiting=0;

  if (!cbuf) {
    ret = -ENOMEM;
  } else {

    proc_entry = create_proc_entry("prodcons",0666, NULL);
    if (proc_entry == NULL) {
      ret = -ENOMEM;
      destroy_cbuffer_t(cbuf);
      printk(KERN_INFO "Prodcons2: No puedo crear la entrada en proc\n");
    } else {
      proc_entry->read_proc = prodcons_read;
      proc_entry->write_proc = prodcons_write;
      printk(KERN_INFO "Prodcons2: Cargado el Modulo.\n");
    }
  }

  return ret;

}


void exit_prodcons_module( void )
{
  remove_proc_entry("prodcons", NULL);
  destroy_cbuffer_t(cbuf);
  printk(KERN_INFO "Prodcons2: Modulo descargado.\n");
}


module_init( init_prodcons_module );
module_exit( exit_prodcons_module );
