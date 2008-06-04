#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>


#ifndef NO_REMOTE_GETHOSTBYNAME
#define remote_gethostbyname gethostbyname
#endif


#define DIE(message) { fprintf(stderr,"error:%s\n",message); \
                      fflush(stderr);\
                      abort(); }


static int is_helper_process
#ifdef STANDALONE_SERVICE
= 1;
#else
= 0;
#endif

typedef enum {
    INVALID = 0,
    HELLO = 50,
    
//     CALL_GETHOSTBYNAME,
    RESULT_GETHOSTBYNAME,
    
    SEND_STRING,
    SEND_INT,
    
//     SHUTDOWN
} MESSAGE_TYPE;

typedef struct {
  MESSAGE_TYPE type;
  unsigned int length;
  char * buffer;
} MESSAGE;

// static void send_message(MESSAGE_TYPE type,unsigned int length,const char* buffer);
// static MESSAGE recv_message();
static void pass_str(FILE*,char**);
static void pass_buffer(FILE*,char**,int);
static void pass_strarray(FILE*,char***);
static void pass_int(FILE*,int*);

/**
 * Send a message with the given values to the given FILE*
 * This message should be read (on the other side of file) with recv_message_file
 * 
 * @param file (output) where to write message to
 * @param type type of message to send
 * @param length length of buffer
 * @param buffer buffer to send
 */
static void send_message_file(FILE* file,MESSAGE_TYPE type,unsigned int length,const char* buffer){
  MESSAGE m;
  m.type = type;
  m.length = length;
  m.buffer = 0;
  fwrite(&m,1,sizeof(MESSAGE),file);
  if(length>0) fwrite(buffer,length,sizeof(char),file);
  fflush(file);
}

/**
 * Read a message from FILE*
 * opposite of send_message_file
 * 
 * @param file (input) file to read message from
 * @return a structure containing the received message
 */
static MESSAGE recv_message_file(FILE* file){
  MESSAGE m;
  m.type = INVALID;
  m.length = 0;
  m.buffer = 0;
  fread(&m,1,sizeof(MESSAGE),file);
  if(m.length > 0){
    m.buffer = malloc(m.length); //user must free this
    fread(m.buffer,m.length,1,file);
  }
  return m;
}

/**
 * Free any allocated buffers used by m
 * 
 * @param m message to free buffers for
 */
static void free_message(MESSAGE m){ 
  if(m.length>0) free(m.buffer); 
  m.length=0;
  m.buffer=0; 
}

/**
 * Calculate the length of a (char*)0 terminated array of char*'s 
 * 
 * @param t array to size up
 * @return size of strarray
 */
static int strarraylen(char** t){
  int i = 0;
  while(t[i] != NULL) ++i;
  return i;
}


/**
 * 
 * deep copy a struct hostent using send_message_file/recv_message_file
 * called on both sides of pipe, leaf funtions implemented differently on 
 * each size to perform actual copy or send.
 * 
 * @param f file the read xor write to
 * @param host structure to send or modify
 */
static void pass_hostent_strings(FILE* f, struct hostent* host){
  //         struct hostent {
  //           char    *h_name;        /* official name of host */
  //           char    **h_aliases;    /* alias list */
  //           int     h_addrtype;     /* host address type */
  //           int     h_length;       /* length of address */
  //           char    *h_addr;  /* list of addresses */
  //         }
  pass_str(f,&host->h_name);
  pass_strarray(f,&host->h_aliases);
  pass_strarray(f,&host->h_addr_list);
  pass_buffer(f,&host->h_addr,host->h_length);
}



/**
 * Run at the same time on both main and helper process
 * 
 * passes char* from helper to main process
 * 
 * @param file file to (read xor write) messages to
 * @param name pointer to string to pass (output on main process)
 */
static void pass_str(FILE* file,char** name){
  if(is_helper_process){
    pass_buffer(file,name,strlen(*name)+1);
  }else{
    pass_buffer(file,name,0);
  }
}


/**
 * Run at the same time on both main and helper process
 * 
 * passes buffer of given size from helper to main process
 * 
 * @param file file to (read xor write) messages to
 * @param name pointer to buffer to pass (output on main process)
 * @param size length of name
 */
static void pass_buffer(FILE* file,char** name,int size){
  if(is_helper_process){
    send_message_file(file,SEND_STRING,size,*name);
  }else{
    MESSAGE m = recv_message_file(file);
    if(m.type != SEND_STRING) DIE("pass_str()");
    //MEMORY LEAK -- MEMORY LEAK -- MEMORY LEAK
    //m.buffer will never be free()'ed
    //MEMORY LEAK -- MEMORY LEAK -- MEMORY LEAK
    *name = m.buffer;
  }
}


/**
 * Run at the same time on both main and helper process
 * 
 * passes int from helper to main process
 * 
 * @param file file to (read xor write) messages to
 * @param x pointer to int to pass or output to
 */
static void pass_int(FILE* file,int* x){
  if(is_helper_process){
    send_message_file(file,SEND_INT,sizeof(int),(char*)x);
  }else{
    MESSAGE m = recv_message_file(file);
    if(m.type != SEND_INT) DIE("pass_int()");
    *x = *((int*)m.buffer);
    free_message(m);
  }
}


/**
 * Run at the same time on both main and helper process
 * 
 * passes null terminated array of strings from helper to main process
 * 
 * @param file file to (read xor write) messages to
 * @param t pointer to string array to pass or output to
 */
static void pass_strarray(FILE* file,char*** t){
  char** array = 0;
  int x=0,i=0;
  
  if(is_helper_process) i = strarraylen(*t);
  
  pass_int(file,&i);
  
  if(!is_helper_process) *t = malloc(sizeof(char*)*(i+1));
  //MEMORY LEAK -- MEMORY LEAK -- MEMORY LEAK
  //*t will never be free()'ed
  //MEMORY LEAK -- MEMORY LEAK -- MEMORY LEAK
  
  
  array = *t;
  array[i]=0; 
      
  for(x=0;x<i;x++)
    pass_str(file,array+x);
  
  
}


#ifndef STANDALONE_SERVICE // NOT DEFINED

/**
 * This is a workaround for a bug causing gethostbyname to fail after a checkpoint/restore
 * It spawns a helper program which calls gethostbyname and passes result back to us.
 * It is dirty, but it works
 * 
 * @param name the hostname to lookup
 * @return structure deep copied from helper process
 */
struct hostent * remote_gethostbyname(const char *name){
  char cmd[1024];
  snprintf(cmd,1023,"./ckpt_services gethostbyname \"%s\"",name);
  
  FILE* helper_process = popen(cmd, "r");
  if(helper_process==0){
    perror("./ckpt_services");
    DIE("Failed to start ckpt_services");
  }
  
  MESSAGE m = recv_message_file(helper_process);
  if(m.type != RESULT_GETHOSTBYNAME) DIE("remote_gethostbyname()");
  
  pass_hostent_strings(helper_process,(struct hostent *)m.buffer);
  pclose(helper_process);
  
  //MEMORY LEAK -- MEMORY LEAK -- MEMORY LEAK
  //m.buffer will never be free()'ed
  //MEMORY LEAK -- MEMORY LEAK -- MEMORY LEAK
  return (struct hostent *)m.buffer;
}

#endif


#ifdef STANDALONE_SERVICE

/**
 * Helper process main routine
 * 
 * 1) perform requested command
 * 2) output resulting messages
 * 3) exit
 * 
 * @param argc size of argv
 * @param argv command to run and arguments for that command
 * @return <>0 on error 
 */
int main( int argc , char ** argv ){
  if(argc<2) DIE("usage: ckpt_services command...");

  if(strcmp(argv[1],"gethostbyname")==0){
    if(argc<3) DIE("usage: ckpt_services gethostbyname name");
    struct hostent * t = gethostbyname(argv[2]);
    send_message_file(stdout,RESULT_GETHOSTBYNAME,sizeof(struct hostent),(char*)t);
    pass_hostent_strings(stdout,t);
    return 0;
  }

  return 0;
}


#endif
