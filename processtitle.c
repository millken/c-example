//from alilua
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <ctype.h>
#include <pwd.h>
#include <unistd.h>
#include <inttypes.h>
#include <sched.h>
//#include "setproctitle.h"
char *process_char_last;
char process_chdir[924];
char process_name[100];
char **_argv;
int _argc;
static int _signal_handler = 0;
extern char **environ;
extern void initproctitle (int argc, char **argv);
extern void setProcTitle (const char *title, int is_master);
void daemonize();
void signal_handler ( int sig );
void active_cpu ( uint32_t active_cpu );
//end h
#ifdef linux

static inline void print_cpu_mask ( cpu_set_t cpu_mask )
{
    uint8_t flag;
    uint32_t i;
    printf ( "%d Cpu affinity is ", getpid() );
    flag = 0;

    for ( i = 0; i < sizeof ( cpu_set_t ); i ++ ) {
        if ( CPU_ISSET ( i, &cpu_mask ) ) {
            if ( flag == 0 ) {
                flag = 1;
                printf ( "%d", i );

            } else {
                printf ( ",%d", i );
            }
        }
    }

    printf ( ".\n" );
}

static inline void get_cpu_mask ( pid_t pid, cpu_set_t *mask )
{
    if ( sched_getaffinity ( pid, sizeof ( cpu_set_t ), mask ) == -1 ) {
        //app_panic("Get cpu affinity failed.\n");
    }
}

static inline void set_cpu_mask ( pid_t pid, cpu_set_t *mask )
{
    if ( sched_setaffinity ( pid, sizeof ( cpu_set_t ), mask ) == -1 ) {
        //app_panic("Set cpu affinity failed.\n");
    }
}
#endif
void active_cpu ( uint32_t active_cpu )
{
#ifdef linux
    cpu_set_t cpu_mask;
    int cpu_count = sysconf ( _SC_NPROCESSORS_ONLN );

    if ( cpu_count < 1 ) {
        return;
    }

    active_cpu = active_cpu % cpu_count;

    get_cpu_mask ( 0, &cpu_mask );
    //print_cpu_mask(cpu_mask);

    CPU_ZERO ( &cpu_mask );
    CPU_SET ( active_cpu, &cpu_mask );
    set_cpu_mask ( 0, &cpu_mask );

    //get_cpu_mask(0, &cpu_mask);
    //print_cpu_mask(cpu_mask);
#endif
}

void initproctitle (int argc, char **argv)
{
    _argc = argc;
    _argv = argv;
	int i;
    size_t n = 0;
	//get process name
#ifdef linux
    n = readlink ( "/proc/self/exe" , process_chdir , sizeof ( process_chdir ) );
#else
    uint32_t new_argv0_s = sizeof ( process_chdir );

    if ( _NSGetExecutablePath ( process_chdir, &new_argv0_s ) == 0 ) {
        n = strlen ( process_chdir );
    }

#endif
	    i = n;

    while ( n > 1 ) if ( process_chdir[n--] == '/' ) {
            strncpy ( process_name, ( ( char * ) process_chdir ) + n + 2, i - n );
            process_chdir[n + 1] = '\0';
            break;
        }

    chdir ( process_chdir );

    n = 0;

    for ( i = 0; argv[i]; ++i ) {
        n += strlen ( argv[i] ) + 1;
    }

    char *raw = malloc ( n );

    for ( i = 0; argv[i]; ++i ) {
        memcpy ( raw, argv[i], strlen ( argv[i] ) + 1 );
        environ[i] = raw;
        raw += strlen ( environ[i] ) + 1;
    }

    process_char_last = argv[0];

    for ( i = 0; i < argc; ++i ) {
        process_char_last += strlen ( argv[i] ) + 1;
    }

    for ( i = 0; environ[i]; ++i ) {
        process_char_last += strlen ( environ[i] ) + 1;
    }

   // return process_chdir;
}

void setProcTitle ( const char *title, int is_master )
{
    _argv[1] = 0;
    char *p = _argv[0];
    memset ( p, 0x00, process_char_last - p );

    if ( is_master ) {
        snprintf ( p, process_char_last - p, "%s: %s %s", process_name, title, process_chdir );
        int i = 1;

        for ( i = 1; i < _argc; i++ ) {
            sprintf ( p, "%s %s", p, environ[i] );
        }

    } else {
        snprintf ( p, process_char_last - p, "%s: %s", process_name, title );
    }
}

void daemonize()
{
    int i, lfp;
    char str[10];

    if ( getppid() == 1 ) {
        return;
    }

    i = fork();

    if ( i < 0 ) {
        exit ( 1 );
    }

    if ( i > 0 ) {
        exit ( 0 );
    }

    setsid();
    signal ( SIGCHLD, SIG_IGN );
    signal ( SIGTSTP, SIG_IGN );
    signal ( SIGTTOU, SIG_IGN );
    signal ( SIGTTIN, SIG_IGN );
    signal ( SIGHUP, signal_handler );
    signal ( SIGTERM, signal_handler );
}

void signal_handler ( int sig )
{
    switch ( sig ) {
        case SIGHUP:
            break;

        case SIGTERM:
            _signal_handler = 1;
            break;
    }
}
static int _workerprocess[200];
static int _workerprocess_count = 0;
static void ( *_workerprocess_func[200] ) ();
int forkProcess ( void ( *func ) () )
{
    int ret = fork();

    if ( ret == 0 ) {
        active_cpu ( _workerprocess_count );
        func ( _workerprocess_count );
    }

    if ( ret > 0 ) {
        _workerprocess[_workerprocess_count] = ret;
        _workerprocess_func[_workerprocess_count] = func;
        _workerprocess_count++;
    }

    return ret;
}

static void worker_main (  )
{

    setProcTitle ( "worker process", 0 );
	sleep(10);
    exit ( 0 );
}
int main(int argc, char *argv[])
{
	int i;
	initproctitle(argc, argv);
    setProcTitle("master process", 1);
	daemonize();
    for ( i = 0; i < 5; i++ ) {
            forkProcess ( worker_main );
    }
	sleep(300);  // you can ps now
    return 0;
}
