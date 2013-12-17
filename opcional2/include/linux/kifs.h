#ifndef KIFS_H
#define KIFS_H
#include <linux/list.h> /* list_head */

#define MAX_KIFS_ENTRY_NAME_SIZE 50 
#define MAX_KIFS_ENTRY_NAME_SIZE_STR "50" 

/* Callback prototypes for kifs entries */
typedef	int (read_kifs_t)(char *user_buffer, unsigned int maxchars, void *data);
typedef	int (write_kifs_t)(const char *user_buffer, unsigned int maxchars, void *data);


/* Descriptor interface for the entries */
typedef struct 
{
	char entryname[MAX_KIFS_ENTRY_NAME_SIZE];
	read_kifs_t *read_kifs;
	write_kifs_t *write_kifs;
	void *data;
	struct list_head links;	/* Set of links in kifs */ 
}kifs_entry_t;

enum {
	KIFS_READ_OP=0,
	KIFS_WRITE_OP,
	KIFS_NR_OPS};


/* This fuction must ensure that no entry will be created as long as another entry with the same name already exists.
 * == Return Value ==
 * NULL Entry name already exists or No space is availables
 * Pointer to the kifs entry
 * */ 
kifs_entry_t* create_kifs_entry(const char* entryname,
				read_kifs_t *read_kifs, 
				write_kifs_t *write_kifs, 
				void* data);


/* Remove kifs entry
 * == Return Value ==
 * -1 Entry does not exist
 *  0 success
 * */ 
int remove_kifs_entry(const char* entry_name);

/*  Implementation of kifs() system call
 * == Return Value ==
 * -EINVAL Unsupported operation (NULL callback) or Entry not exists
 * -EFAULT Any other error (e.g: copy_from_user(), copy_to_user(),...)
 * otherwise: Number of chars read/written (Usually maxchars value)
 */
asmlinkage long sys_kifs(const char* entry_name,unsigned int op_mode, char* user_buffer,unsigned int maxchars);

/* KIFS's global initialization */
void init_kifs_entry_set(void);


#endif
