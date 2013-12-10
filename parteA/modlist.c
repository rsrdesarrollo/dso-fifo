#include <linux/proc_fs.h>
#include <asm-generic/uaccess.h>
#include <linux/spinlock.h>

#include "modlist.h"

MODULE_AUTHOR("R.S.R.");
MODULE_DESCRIPTION("Implementa una lista de enteros en un módulo"\
        "de kernel administrable por una entrada en /proc");
MODULE_LICENSE("GPL");

struct proc_dir_entry *modlist_proc_entry;

LIST_HEAD(mylist); 


DEFINE_SPINLOCK(mutex_list);

/*   ###########################################
 *   Funciones de carga y descarga del módulo
 *   -------------------------------------------
 */
int modlist_init(void){
    DBG("[modlist] Cargado. Creando entrada /proc/modlist");
    modlist_proc_entry = create_proc_entry(proc_entry_name,0666,NULL);
    modlist_proc_entry->read_proc = procfile_read;
    modlist_proc_entry->write_proc = procfile_write;
    return 0;
}

void modlist_clean(void){
    DBG("[modlist] Descargado. Eliminando entrada /proc/modlist");
    remove_proc_entry(proc_entry_name, NULL);
    safe_clear_list(&mylist);
}

module_init(modlist_init);
module_exit(modlist_clean);


/*   ##########################################
 *   Funciones de manejo de entrada /proc
 *   ------------------------------------------
 */
int procfile_read(char *buffer, char **buffer_location,
        off_t offset, int buffer_len, int *eof, void *data)
{
    list_item_t *elem = NULL;
    int used = 0;

    DBG("[modlist] evento de lectura [max]: %d char", buffer_len);

    spin_lock(&mutex_list);   // Entra sección crítica

    list_for_each_entry(elem, &mylist, links){
        if(buffer_len - used == 0) 
            break;
        used += snprintf(buffer + used, buffer_len - used, 
                "%d\n", elem->data);	
    }

    spin_unlock(&mutex_list); // Sale sección crítica

    DBG("[modlist] evento de lectura [used]: %d char", used);

    return used;
}

int procfile_write(struct file *file, const char __user *buffer, 
        unsigned long count, void *data)
{
    char line[25] = ""; // Un entero no puede ocupar más de 10 caracteres
    int num = 0;

    DBG("[modlist] evento de escritura: %lu char", count);

    copy_from_user(line, buffer, min(count,24UL));
    line[min(count,24UL)] = 0;
    DBG("[modlist] se han copiado al buffer del kernel: %lu char", min(count,24UL));

    if(sscanf(line, "add %d", &num)){
        add_item_list(&mylist, num);
    }else if(sscanf(line, "remove %d", &num)){
        remove_items_list(&mylist, num);
    }else if(streq(line,"cleanup")){
        safe_clear_list(&mylist);
    }

    DBG("\t evento realizado }:)");

    return min(count,24UL);
}



/*   #############################################
 *   Funciones de manejo de lista
 *   ----------------------------------------------
 */

// Llámame solo si la lista no es compartida
void _unsafe_clear_list(struct list_head *list){
    list_item_t *aux, *elem = NULL;

    list_for_each_entry_safe(elem, aux, list, links){
        list_del(&(elem->links));
        vfree(elem);
    }

}


void safe_clear_list(struct list_head *list){
    LIST_HEAD(bin);
    list_item_t *aux, *elem = NULL;
    DBG("[modlist] Clear list.");

    spin_lock(&mutex_list);     // Entra en la sección crítica

    list_for_each_entry_safe(elem, aux, list, links){
        list_move(&(elem->links), &bin);
    }

    spin_unlock(&mutex_list);   // Sale de la sección critica

    _unsafe_clear_list(&bin);
}

void add_item_list(struct list_head *list, int data){
    list_item_t *item = vmalloc(sizeof(list_item_t));
    item->data = data;

    spin_lock(&mutex_list);
    list_add(&(item->links),list);
    spin_unlock(&mutex_list);
}

void remove_items_list(struct list_head *list, int data){
    LIST_HEAD(bin);
    list_item_t *aux, *elem = NULL;

    spin_lock(&mutex_list);
    list_for_each_entry_safe(elem, aux, list, links){
        if(elem->data == data)
            list_move(&(elem->links), &bin);
    }
    spin_unlock(&mutex_list);

    _unsafe_clear_list(&bin);
}

// Si no me vas a llamar con la cadena terminada en \0 me daré 
//  un paseo por el kernel
int streq(char *a, char *b){
    while (*a && *b && *a == *b && a++ && b++);
    return !(*a && *b);
}
