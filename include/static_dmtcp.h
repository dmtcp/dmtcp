#ifndef STATIC_DMTCP_H
#define STATIC_DMTCP_H

#include <mqueue.h>
#include <signal.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/socket.h>
#include <poll.h>
#include <sys/wait.h>

int accept_next(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int accept4_next(int sockfd, struct sockaddr *addr,
                 socklen_t *addrlen, int flags);
int bind_next(int sockfd, const struct sockaddr *my_addr, socklen_t addrlen);
int clock_getcpuclockid_next(pid_t pid, clockid_t *clock_id);
int clone_next(int (*function)(void *), void *child_stack, int flags,
               void *arg, int *parent_tidptr,
               struct user_desc *newtls, int *child_tidptr);
int close_next(int fd);
int closedir_next(DIR *d);
void closelog_next(void);
int connect_next(int sockfd, const struct sockaddr *serv_addr,
                 socklen_t addrlen);
int dlclose_next(void *handle);
void * dlopen_next(const char *filename, int flag);
void * dlsym_next(void *handle, const char *symbol);
int dup_next(int oldfd);
int dup2_next(int oldfd, int newfd);
int dup3_next(int oldfd, int newfd, int flags);
int execv_next(const char *path, char *const argv[]);
int execve_next(const char *filename, char *const argv[], char *const envp[]);
int execvp_next(const char *file, char *const argv[]);
int execvpe_next(const char *file, char *const argv[], char *const envp[]);
void exit_next(int status);
int fclose_next(FILE *fp);
int fcntl_next(int fd, int cmd, void *arg);
int fexecve_next(int fd, char *const argv[], char *const envp[]);
FILE * fopen_next(const char *path, const char *mode);
FILE * fopen64_next(const char *path, const char *mode);
pid_t fork_next(void);
pid_t getpgid_next(pid_t pid);
pid_t getpgrp_next(void);
pid_t getpid_next(void);
pid_t getppid_next(void);
int getpt_next(void);
pid_t getsid_next(pid_t pid);
int getsockopt_next(int s, int level, int optname,
                    void *optval, socklen_t *optlen);
pid_t gettid_next(void);
int ioctl_next(int fd, unsigned long request, ...);
int kill_next(pid_t pid, int sig);
int listen_next(int sockfd, int backlog);
int lxstat_next(int vers, const char *path, struct stat *buf);
int lxstat64_next(int vers, const char *path, struct stat64 *buf);
int mkstemp_next(char *templ);
void * mmap_next(void *addr, size_t length, int prot, int flags,
                 int fd, off_t offset);
void * mmap64_next(void *addr, size_t length, int prot,
                   int flags, int fd, __off64_t offset);
int mq_close_next(mqd_t mqdes);
int mq_notify(mqd_t mqdes, const struct sigevent *sevp);
mqd_t mq_open_next(const char *name, int oflag, ...);
ssize_t mq_timedreceive_next(mqd_t mqdes, char *msg_ptr, size_t msg_len,
                       unsigned *msg_prio, const struct timespec *abs_timeout);
int mq_timedsend_next(mqd_t mqdes, const char *msg_ptr, size_t msg_len,
                        unsigned msg_prio, const struct timespec *abs_timeout);
void *mremap_next(void *old_address, size_t old_size, size_t new_size,
                  int flags, ... /* void *new_address */);
int msgctl_next(int msqid, int cmd, struct msqid_ds *buf);
int msgget_next(key_t key, int msgflg);
ssize_t msgrcv_next(int msqid, void *msgp, size_t msgsz,
                    long msgtyp, int msgflg);
int msgsnd_next(int msqid, const void *msgp, size_t msgsz, int msgflg);
int munmap_next(void *addr, size_t length);
int open_next(const char *pathname, int flags, ...);
int open64_next(const char *pathname, int flags, mode_t mode);
int openat_next(int dirfd, const char *pathname, int flags, ...);
int openat64_next(int dirfd, const char *pathname, int flags, mode_t mode);
DIR *opendir_next(const char *name);
// openlog
// pclose
int poll_next(struct pollfd*, nfds_t, int);
// popen
int posix_openpt_next(int flags);
// process_vm_readv
// process_vm_writev
int pthread_cond_broadcast_next(pthread_cond_t *cond);
// pthread_cond_destroy
// pthread_cond_init
int pthread_cond_signal_next(pthread_cond_t *cond);
int pthread_cond_timedwait_next(pthread_cond_t *cond, pthread_mutex_t *mutex,
                                const struct timespec *abstime);
int pthread_cond_wait_next(pthread_cond_t *cond, pthread_mutex_t *mutex);
int pthread_create_next(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine) (void *), void *arg);
void pthread_exit_next(void *retval);
// pthread_getspecific
int pthread_mutex_lock_next(pthread_mutex_t *mutex);
int pthread_mutex_trylock_next(pthread_mutex_t *mutex);
int pthread_mutex_unlock_next(pthread_mutex_t *mutex);
// pthread_rwlock_rdlock
// pthread_rwlock_tryrdlock
// pthread_rwlock_trywrlock
// pthread_rwlock_unlock
// pthread_rwlock_wrlock
// pthread_sigmask
// pthread_timedjoin_np
// pthread_tryjoin_np
// ptrace
int ptsname_r_next(int fd, char *buf, size_t buflen);
ssize_t read_next(int fd, void *buf, size_t count);
ssize_t readlink_next(const char *pathname, char *buf, size_t bufsiz);
// sched_getaffinity
// sched_getattr
// sched_getparam
// sched_getscheduler
// sched_setaffinity
// sched_setattr
// sched_setparam
// sched_setscheduler
int select_next(int, fd_set*, fd_set*, fd_set*, struct timeval*);
int semctl_next(int semid, int semnum, int cmd, ...);
int semget_next(key_t key, int nsems, int semflg);
int semop_next(int semid, struct sembuf *sops, size_t nsops);
int semtimedop_next(int semid, struct sembuf *sops, size_t nsops,
                    const struct timespec *timeout);
// setgid
int setpgid_next(pid_t pid, pid_t pgid);
int setpgrp_next(void);
// setsid
int setsockopt_next(int sockfd, int level, int optname,
                    const void *optval, socklen_t optlen);
// setuid
void * shmat_next(int shmid, const void *shmaddr, int shmflg);
int shmctl_next(int shmid, int cmd, struct shmid_ds *buf);
int shmdt_next(const void *shmaddr);
int shmget_next(key_t key, size_t size, int shmflg);
int sigaction_next(int signum, const struct sigaction *act,
                   struct sigaction *oldact);
int sigblock_next(int mask);
int siggetmask_next(void);
int sighold_next(int sig);
int sigignore_next(int sig);
sighandler_t signal_next(int signum, sighandler_t handler);
int sigpause_next(int sigmask);
// _sigpause
int sigprocmask_next(int how, const sigset_t *set, sigset_t *oldset);
int sigrelse_next(int sig);
sighandler_t sigset_next(int sig, sighandler_t disp);
int sigsetmask_next(int mask);
int sigsuspend_next(const sigset_t *mask);
int sigtimedwait_next(const sigset_t *set, siginfo_t *info,
                      const struct timespec *timeout);
// sigvec
int sigwait_next(const sigset_t *set, int *sig);
int sigwaitinfo_next(const sigset_t *set, siginfo_t *info);
int socket_next(int domain, int type, int protocol);
int socketpair_next(int d, int type, int protocol, int sv[2]);
long syscall_next(long sys_num, ...);
int system_next(const char *cmd);
// tcgetpgrp
// tcgetsid
// tcsetpgrp
int tgkill_next(int tgid, int tid, int sig);
// timer_create
int tkill_next(int tid, int sig);
int ttyname_r_next(int fd, char *buf, size_t buflen);
// wait
// wait3
pid_t wait4_next(pid_t pid, int * status, int options, struct rusage *rusage);
int waitid_next(idtype_t idtype, id_t id, siginfo_t *infop, int options);
// waitpid
ssize_t write_next(int fd, const void *buf, size_t count);
int xstat_next(int vers, const char *path, struct stat *buf);
int xstat64_next(int vers, const char *path, struct stat64 *buf);

//  EXTRA
struct hostent * gethostbyaddr_next(const void *addr, socklen_t len, int type);
struct hostent *gethostbyname_next(const char *name);
void dmtcp_initialize_plugin_next();
int epoll_wait_next(int epfd, struct epoll_event *events,
                    int maxevents, int timeout);
int epoll_ctl_next(int epfd, int op, int fd, struct epoll_event *event);
int epoll_create_next(int size);
int epoll_create1_next(int flags);
int access_next(const char *pathname, int mode);
int eventfd_next(unsigned int initval, int flags);
int signalfd_next(int fd, const sigset_t *mask, int flags);
int getnameinfo_next(const struct sockaddr*, socklen_t, char*,
                     size_t, char*, size_t, int);
int pselect_next(int, fd_set*, fd_set*, fd_set*, const struct timespec*,
                 const sigset_t*);
int getaddrinfo_next(const char*, const char*, const struct addrinfo*,
                     struct addrinfo**);
int __poll_chk_next(struct pollfd*, nfds_t, int, size_t);
int __register_atfork_next (void (*prepare) (void),
                            void (*parent) (void), void (*child) (void),
                            void *dso_handle);
void *malloc_next(size_t size);
void free_next(void *ptr);
void *calloc_next(size_t nmemb, size_t size);
void *realloc_next(void *ptr, size_t size);
int pthread_rwlock_trywrlock_next(pthread_rwlock_t *rwlock);
int pthread_rwlock_wrlock_next(pthread_rwlock_t *rwlock);
int pthread_rwlock_rdlock_next(pthread_rwlock_t *rwlock);
int pthread_rwlock_tryrdlock_next(pthread_rwlock_t *rwlock);
int pthread_rwlock_unlock_next(pthread_rwlock_t *rwlock);
void *pthread_getspecific_next(pthread_key_t key);
int pthread_setspecific_next(pthread_key_t key, const void *value);
int pthread_sigmask_next(int how, const sigset_t *set, sigset_t *oldset);
int pthread_tryjoin_np_next(pthread_t thread, void **retval);
int pthread_timedjoin_np_next(pthread_t thread, void **retval,
                         const struct timespec *abstime);
int unsetenv_next(const char *name);
#endif
