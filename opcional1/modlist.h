#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/types.h>
#include <linux/list.h>


//#define MODLIST_DEBUG
#ifdef MODLIST_DEBUG
#define DBG(format, arg...) do { \
			printk(KERN_DEBUG "%s: " format "\n" , __func__ , ## arg); \
		} while (0)
#else
#define DBG(format, arg...) /* */
#endif

#define proc_entry_name "modlist"
int streq(char *a, char *b);

/*
 *  Definición de manejo de la lista
 */
typedef struct {
       int data;
       struct list_head links;
}list_item_t;

/*
 * borra un elemento de la liasta de manera no bloqueante  
 */
void _unsafe_clear_list(struct list_head *list);

/*
 *	Funciones de manejo de lista
 *	--------------------------------------
 */
void safe_clear_list(struct list_head *list);
void add_item_list(struct list_head *list, int data);
void remove_items_list(struct list_head *list, int data);

/*
 *   Manejo de la entrada en /proc
 *   -------------------------------------
 */
int procfile_read(char *buffer, char **buffer_location,
		  off_t offset, int buffer_len, int *eof, void *data);

int procfile_write(struct file *file, const char __user *buffer, 
		   unsigned long count, void *data);




