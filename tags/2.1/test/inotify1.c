/******************************************************************
 * File:   inotify_simple_test.c
 * Author: onyeka
 *
 * Created on Apr 13, 2012, 2:29:43 PM
 *
 *  Simple inotify test
 *
 *  This program (crudely) demonstrates how inotify would work
 *  when using dmtcp to check point a program
 *
 *  The test opens up a single inotify instance and adds several
 *  files to be watched to it. The program could be killed and 
 *  restarted from where it stopped before being killed.
 ******************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/inotify.h>

#define SUCCESS      0
#define FAILURE     -1
#define NUM_OF_FILES 3
#define FILENAME_LEN 7
#define EVENT_SIZE (sizeof (struct inotify_event)) //size of event struct
#define BUF_LEN    ( 1024 *(EVENT_SIZE + 16) ) 

static  const char file_names_1[NUM_OF_FILES][FILENAME_LEN] = {"bake_0", 
                                                               "bake_1", 
                                                               "bake_2"};
static  const char file_names_2[NUM_OF_FILES][FILENAME_LEN] = {"Samm_3", 
                                                               "Samm_4", 
                                                               "Samm_5"};
typedef struct arguments
{
   int fd;
   int wd;
}arguments;

/*-------------------------function prototypes--------------------*/
void* monitor_access_to_files(void* thread_args);


/******************************************************************
 * function name: main()
 * 
 * description:   Entry point of the test program
 *                
 * para:          argc - number of parameters from command line
 * para:          argv - array of the parameters
 * return:        ret_val SUCCESS or FAILURE
 ******************************************************************/
int main(int argc, char** argv) 
{
   FILE* file_p;
   int i, fd,wd_1, wd_2, ret_val;
   arguments thread_args_1 = {0};
   arguments thread_args_2 = {0};
   pthread_t monitor_thread_1;
   pthread_t monitor_thread_2;
   char directory_1[] = "/tmp/dmtcp_inotify1/";
   char directory_2[] = "/tmp/dmtcp_inotify2/";

   
   //create the directory in /tmp which will host the files
   if((ret_val = mkdir(directory_1, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)) != SUCCESS)
   {
      printf("couldn't create the directory...it probably already exists\n");
   }
   
   if((ret_val = mkdir(directory_2, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)) != SUCCESS)
   {
      printf("couldn't create the directory...it probably already exists\n");
   }
  
   //create an inotify instance
   if((fd = inotify_init()) < 0)
   {
      perror("inotify_init");
      ret_val = FAILURE;
      goto INOTIFY_SIMPLE_TEST_LBL_EXIT;
   }
   printf("fd created:%d\n", fd); 
   
   /*========================================
    * create The first watch descriptor 
    * and it's monitor thread
    *========================================*/ 
   wd_1 = inotify_add_watch(fd, directory_1, 
                          IN_OPEN | IN_CLOSE | IN_MODIFY);
   if(wd_1 < 0)
   {
      perror("inotify_add_watch");
      ret_val = FAILURE;
      goto INOTIFY_SIMPLE_TEST_LBL_EXIT;
   }
   printf("wd:%d created for path:%s and fd:%d\n", wd_1, directory_1, fd);
   
   //save the fd and wd in the argument structure
   thread_args_1.fd = fd;
   thread_args_1.wd = wd_1;
   
   //create a thread to monitor whenever a file in this directory is touched
   if( (ret_val = pthread_create(&monitor_thread_1, NULL, monitor_access_to_files, 
                                 (void*)&thread_args_1)) != SUCCESS)
   {
      perror("pthread_create");
      goto INOTIFY_SIMPLE_TEST_LBL_EXIT;
   }
   
   //detach the threads
   if((ret_val = pthread_detach(monitor_thread_1)) != SUCCESS)
   {
      perror("pthread_detach");
      goto INOTIFY_SIMPLE_TEST_LBL_EXIT;
   }
   
   /*========================================
    * create The second watch descriptor 
    * and it's monitor thread
    *========================================*/ 
   wd_2 = inotify_add_watch(fd, directory_2, 
                          IN_OPEN | IN_CLOSE | IN_MODIFY);
   if(wd_2 < 0)
   {
      perror("inotify_add_watch");
      ret_val = FAILURE;
      goto INOTIFY_SIMPLE_TEST_LBL_EXIT;
   }
   printf("wd:%d created for path:%s and fd:%d\n", wd_2, directory_1, fd);
   
   //save the fd and wd in the argument structure
   thread_args_2.fd = fd;
   thread_args_2.wd = wd_2;
   
   //create a thread to monitor whenever a file in this directory is touched
   if( (ret_val = pthread_create(&monitor_thread_2, NULL, monitor_access_to_files, 
                                 (void*)&thread_args_2)) != SUCCESS)
   {
      perror("pthread_create");
      goto INOTIFY_SIMPLE_TEST_LBL_EXIT;
   }
   
   //detach the threads
   if((ret_val = pthread_detach(monitor_thread_2)) != SUCCESS)
   {
      perror("pthread_detach");
      goto INOTIFY_SIMPLE_TEST_LBL_EXIT;
   }
   
   /*=========================================================
    * This will ensure that files don't get too big
    * by creating the files afresh whenever the program is run
    *=========================================================*/
   for(i = 0; i < NUM_OF_FILES; i++)
   {
      char name_path_1[20] = {0};
      char name_path_2[20] = {0};
      strcat(name_path_1, directory_1);
      strcat(name_path_1, file_names_1[i]);
      strcat(name_path_2, directory_2);
      strcat(name_path_2, file_names_2[i]); 
      
      //write to file in directory 1
      file_p = fopen(name_path_1, "w");
      fprintf(file_p, "Testing...%d ", i);
     // printf("characters written:%d\n", ret_val);
      fclose(file_p);
      
      //write to file in directory 2
      file_p = fopen(name_path_2, "w");
      fprintf(file_p, "Testing...%d ", i+NUM_OF_FILES);
     // printf("characters written:%d\n", ret_val);
      fclose(file_p);
   }

   //in a loop, write some text into each file
   while(1)
   {
      for(i = 0; i < NUM_OF_FILES; i++)
      {
         char name_path_1[20] = {0};
         char name_path_2[20] = {0};
         strcat(name_path_1, directory_1);
         strcat(name_path_1, file_names_1[i]);
         strcat(name_path_2, directory_2);
         strcat(name_path_2, file_names_2[i]); 
      
         //write to file in directory 1
         file_p = fopen(name_path_1, "a+");
         fprintf(file_p, "Testing...%d ", i);
         // printf("characters written:%d\n", ret_val);
         fclose(file_p);
      
         //write to file in directory 2
         file_p = fopen(name_path_2, "a+");
         fprintf(file_p, "Testing...%d ", i+NUM_OF_FILES);
         // printf("characters written:%d\n", ret_val);
         fclose(file_p);
         sleep(3);
      }
      
   }

INOTIFY_SIMPLE_TEST_LBL_EXIT:
   return ret_val;
}


/******************************************************************
 * function name: monitor_access_to_files()
 * 
 * description:   Entry point of the monitor thread
 *                
 * para:          void
 * return:        void
 ******************************************************************/
void* monitor_access_to_files(void* thread_args)
{
   int len, event_index = 0;
   char buffer[BUF_LEN] = {0};
   struct inotify_event *event;
   arguments* params = (arguments*) thread_args;
   
   if(params == NULL)
   {
      printf("thread parameters are NULL");
      pthread_exit(NULL);
   }
   // wait for an event and when it happens, print out what happened
   while(1)
   {
      len = read(params->fd, buffer, BUF_LEN);
      
      if(len < 0)
      {
         perror("read");
      }
      else if(!len)
      {
         printf("check if buffer is too small...\n\n");
      }
      
      
      printf("*=====================BEGIN==================*\n");
      
      
      //print out all the events that happened
      while(event_index < len)
      {
         event = (struct inotify_event *)&buffer[event_index];

         printf("fd=%d wd=%d bytes read=%d ==>",params->fd, event->wd, len);
         
        //determine what event occurred
         if(event->mask & IN_OPEN)
         {
            printf("file: %s was opened\n", event->name);
         }
         else if(event->mask & IN_CLOSE)
         {
            printf("file: %s was closed\n", event->name);
         }
         else if(event->mask & IN_MODIFY)
         {
            printf("file: %s was modified\n", event->name);
         }
         else
         {
            printf("unknown event occurred:%d, event name:%s", event->mask, event->name);
         }
         event_index += EVENT_SIZE + event->len;
      }
      event_index = 0;
      printf("*======================END===================*\n\n\n");
   }
}