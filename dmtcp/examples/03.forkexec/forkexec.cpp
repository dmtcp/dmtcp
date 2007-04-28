/***************************************************************************
 *   Copyright (C) 2007 by Jason Ansel                                     *
 *   jansel@ccs.neu.edu                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
       
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

static const int WELL_KNOWN_FD = 10;

static void die(const char* msg)
{
    printf("ERROR: %s \n",msg);
    _exit(-1);
}

int main(int argc, char** argv)
{
    FILE* fp = NULL;
    
    if(argc == 1)
    {//master
        
        int fd[2];
        if(socketpair(AF_UNIX,SOCK_STREAM,0,fd) != 0) die("pipe() failed");
        
//         char buf[] = "TEST";
//         write(fd[1], "test", sizeof buf);
//         read(fd[0], buf, sizeof buf);
//         
//         printf("%s\n",buf);
             
        
        if(fork() == 0)
        {
            //oops... the user forgot to close a unused socket!!!
            //close(fd[1]); 
            dup2(fd[0],WELL_KNOWN_FD);
            char* t[] = { argv[0] , "slave", 0};
	    // BAD USER: just trashed our environment variables:
            char* env[] = {"A=B","C=E","LD_PRELOAD=taco",0};
            execve( argv[0], t , env);
            perror("exec()");
            die("exec failed");

        }
        else
        {
            close(fd[0]);
            dup2(fd[1],WELL_KNOWN_FD);
 
            char c;
            
            while(read(0,&c,1))
            {
//                 if(c=='\n') c = '\n';
//                 printf("sending: %c\n",c);
                write(WELL_KNOWN_FD, &c, 1);
            }
            
            die("master done");
            
        }
    }

    //client
    {
//         char* lineptr = NULL;
//         size_t n = 0;
        fp = fdopen(WELL_KNOWN_FD,"rw");
        if(fp == NULL) die("fdopen failed");
        
        char c;
        
        while(read(WELL_KNOWN_FD,&c,1))
        {
            printf(" %c",c);
        }
        
        die("client done");
    }
    
    return 0;
}
