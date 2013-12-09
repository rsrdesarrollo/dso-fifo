#include "dump_utmp.h"

char *nombre_programa;

bool incluye_cabecera=false;

bool just_print=false;

/* Devuelve un string con la fecha/hora.  */
static char *
string_tiempo (const struct utmp *utmp_ent)
{
	static char strtime[100];
	char* ptrtime;
	time_t tiempo;
	int len=0;
	
	tiempo=utmp_ent->ut_time;		
	ptrtime=ctime(&tiempo);
	strcpy(strtime,ptrtime);
	len=strlen(strtime);
	strtime[len-1]='\0';	/* Remove the new line character */
	return strtime;
}


/* Imprime una linea de salida formateada. */
static void
print_entry (const char *type,const char *usuario, const char *linea, const char *strtiempo,
	    const char *idlinea,const char *host,const char* str_salida)
{
	/* CODIGO */
	printf("(%s)\t%s\t\t%s\t%s\t%c%c\t%s\t%s\n",type,usuario,linea,strtiempo,idlinea[0],idlinea[1],host,str_salida);
}

static void
print_header (void)
{
	printf("TIPO\t\tNOMBRE\t\tLINEA\tTIEMPO\t\t\t\tID.\tHOST\tSALIDA\n");
	printf("-----------------------------------------------------------------------------------------\n");
}

static void utmptype2str(char* buf,short type)
{
	switch(type)
	{
	case UT_UNKNOWN: strcpy(buf,"UT_UNKNOWN");
		break;
	case RUN_LVL: strcpy(buf,"RUN_LVL");
		break;
	case BOOT_TIME: strcpy(buf,"BOOT_TIME");
		break;
	case NEW_TIME: strcpy(buf,"NEW_TIME");
		break;
	case OLD_TIME: strcpy(buf,"OLD_TIME");
		break;
	case INIT_PROCESS: strcpy(buf,"INIT_PROCESS");
		break;
	case LOGIN_PROCESS: strcpy(buf,"LOGIN_PROCESS");
		break;
	case USER_PROCESS: strcpy(buf,"USER_PROCESS");
		break;
	case DEAD_PROCESS: strcpy(buf,"DEAD_PROCESS");
		break;
	case ACCOUNTING: strcpy(buf,"ACCOUNTING");
		break;
	default: strcpy(buf,"OTHER");
	}
}


/* Imprime el array de registros UTMP_BUF, que consta de n entradas. */

static void
print_utmp_entries (size_t n_entries, const struct utmp *utmp_buf)
{
	int i=0;
	char strtype[100];
	char strexit[100];
	char *strtime;
	const struct utmp *cur;

	/* CODIGO */
	if (incluye_cabecera)
		print_header();

	for (i=0;i<n_entries;i++)
	{
		cur=&(utmp_buf[i]);
		/* Build String Representation */
		utmptype2str(strtype,cur->ut_type);	
		strtime=string_tiempo(cur);
		sprintf(strexit,"(%i,%i)",cur->ut_exit.e_termination,cur->ut_exit.e_exit);
		print_entry (strtype,cur->ut_user, cur->ut_line,strtime,cur->ut_id,cur->ut_host,strexit);	
	}

	
	
}


int isdir_path(char *dir)
{
	DIR *dirp=NULL;
	int ret=0;

	// Abre el directorio dir, y obtiene un descriptor dirp de tipo DIR
	if ((dirp = opendir(dir)) == NULL) { 
		// Check errno
		if (errno==ENOTDIR)
		{
			/* This is not a directory*/
			ret=0;
		}
		else {
			ret=-1; perror(dir); exit(-1); 
		}
	}
	else{
		ret=1;
		closedir(dirp);
	}

	return ret;

}




/* Lee el fichero especificado y almcena su contenido 
   en un array de STRUCT UTMP */
static int 
read_utmp(char *fichero, int *n_entradas, struct utmp **utmp_buf) {
  /* CODIGO */
  int fd_utmp=0; 
  FILE* file_utmp=NULL;
  struct stat stat_str;
  int num_registers=0;
  struct utmp *utmpv=NULL;


/* Abre fichero usando API del SO */
  if ((fd_utmp=open(fichero,O_RDONLY))<0)
  {
	perror("Utmp file could not be opened");
	exit(-1);
  }
	
/* Obtiene FILE* de la biblioteca estandar */
  file_utmp=fdopen(fd_utmp,"r");
    
/* Obtiene información del descriptor */
  if (fstat(fd_utmp, &stat_str))
  {
	perror("The status information could not be obtained");
	exit(-1);
  }  

  num_registers=((int)stat_str.st_size)/sizeof(struct utmp);  

  if ((utmpv=((struct utmp*)malloc(num_registers*sizeof(struct utmp))))==NULL)
  {
	perror("Not available memory for the UTMP buffer");
	exit(-1);
  }

/* Lee el contenido del fichero UTMP y lo almacena en un buffer */
  if (fread(utmpv,sizeof(struct utmp),num_registers,file_utmp) != num_registers)
  {
	perror("Error while trying to access this stuff");
	exit(-1);
  }

  fclose(file_utmp);

 /* Devuelve los datos */
  *utmp_buf=utmpv;
  *n_entradas=num_registers;

  return 0;
}

/* Lee los registros del fichero UTMP y los envía por un FIFO, uno a uno */
static void
utmp_send (char *fichero)
{
  int n_entradas;
  int i;
  struct utmp *utmp_buf;
  const struct utmp *cur;
  char *real_path;
  int fd_fifo=0;
  int bytes=0;
  const int size=sizeof(struct utmp);

  if (isdir_path(fichero))
  {
	real_path=malloc(strlen(fichero)+5+1);
	sprintf(real_path,"%s/utmp",fichero);
  }
  else {
	real_path=malloc(strlen(fichero)+1);
	strcpy(real_path,fichero);
   }

  /* Read utmp */
  if (read_utmp(real_path, &n_entradas, &utmp_buf) != 0)
    err (EXIT_FAILURE, "%s", fichero);


  if (just_print){
	print_utmp_entries(n_entradas,utmp_buf);
  }
  else{
	fd_fifo=open(PATH_FIFO,O_WRONLY);

  	if (fd_fifo<0){
		perror(PATH_FIFO);
		exit(1);
  	}

    /* Bucle de envío de datos a través del FIFO */
   	for (i=0;i<n_entradas;i++)
   	{
		cur=&(utmp_buf[i]);
		bytes=write(fd_fifo,cur,size);

		if (bytes > 0 && bytes!=size)
 		{
			fprintf(stderr,"Can't read the whole register\n");
			exit(1);
  		}else if (bytes < 0)
  		{
			fprintf(stderr,"Error when writing to the FIFO\n");
			exit(1);
  		}	
  	}
  
  	close(fd_fifo);
  }
  free(utmp_buf);
  free(real_path);

}

/* Recibe un conjunto de registros UTMP a través de un FIFO y e imprime su contenido por pantalla */
static void
utmp_receive (void)
{
  struct utmp received;
  char strtype[50];
  char strexit[50];
  char* strtime;
  int fd_fifo=0;
  int bytes=0;
  const int size=sizeof(struct utmp);

  fd_fifo=open(PATH_FIFO,O_RDONLY);

  if (fd_fifo<0){
	perror(PATH_FIFO);
	exit(1);
  }

 if (incluye_cabecera)
	print_header();

  while((bytes=read(fd_fifo,&received,size))==size)
  {
	/* Build String Representation */
	utmptype2str(strtype,received.ut_type);	
	strtime=string_tiempo(&received);
	sprintf(strexit,"(%i,%i)",received.ut_exit.e_termination,received.ut_exit.e_exit);
	print_entry (strtype,received.ut_user, received.ut_line,strtime,received.ut_id,received.ut_host,strexit);	
 }

  if (bytes > 0)
  {
	fprintf(stderr,"Can't read the whole register\n");
	exit(1);
  }else if (bytes < 0)
  {
	fprintf(stderr,"Error when reading from the FIFO\n");
	exit(1);
  }
	
   close(fd_fifo);
}

static void
uso (int status)
{
  if (status != EXIT_SUCCESS)
    warnx("Pruebe `%s -h' para obtener mas informacion.\n", nombre_programa);
  else
    {
      printf ("Uso: %s [OPCION]... [ FICHERO | ARG1 ARG2 ]\n", nombre_programa);
      fputs ("\
Vuelca contenido fichero UTMP.\n\
", stdout);
      fputs ("\
  -H, 	imprime cabecera\n\
", stdout);
      fputs ("\
  -r,  el proceso actúa como receptor de los registros utmp por el FIFO\n\
  -s,  el proceso envía los resgistros leidos por un FIFO\n\
  -p,  solo impresion por pantalla de los registros del fichero\n\
", stdout);
      fputs ("\
  -h,	Muestra este breve recordatorio de uso\n\
", stdout);
      printf ("\
\n\
Si no se especifica FICHERO, usa %s. Es habitual el uso de %s como FICHERO.\n",UTMP_FILE, WTMP_FILE);
    }
  exit (status);
}

int
main (int argc, char **argv)
{
  int optc;
  bool receive=false;
  nombre_programa = argv[0];

  while ((optc = getopt (argc, argv, "psrHh")) != -1)
    {
      switch (optc)
	{
	case 'H':
	  incluye_cabecera = true;
	  break;

	case 'h':
	  uso(EXIT_SUCCESS);

	case 'r':
	  receive=true;
	  break;

	case 's':
	  receive=false;
	  break;	

	case 'p':
	  just_print=true;
	  break;	

	default:
	  uso (EXIT_FAILURE);
	}
    }


  if (receive)
	utmp_receive();
  else
  	switch (argc - optind)
    	{
    		case 0:			/* dump_utmp */
      		utmp_send (UTMP_FILE);
     		 break;

    		case 1:		/* dump_utmp  <utmp_file> */
      		utmp_send (argv[optind]);
      		break;
	
    		default:	
      		warnx ("Demasiados parametros: %s", argv[optind + 2]);
      		uso (EXIT_FAILURE);
    	}

  exit (EXIT_SUCCESS);
}
