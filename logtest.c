#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdarg.h> //va_start va_end
//#include <math.h>

#define FG_WHITE     	"\x1B[0;m"   /* white */
#define FG_RED      	"\033[0;31m"  /* 0 -> normal ; 31 -> red */
#define FG_RED_BOLD 	"\033[1;31m" /* 1 -> bold ; 31 -> red */
#define FG_GREEN    	"\033[0;32m"  /* 4 -> underline ; 32 -> green */
#define FG_GREEN_BOLD   "\033[1;32m"
#define FG_YELLOW 		"\033[0;33m"  /* 0 -> normal ; 33 -> yellow */
#define FG_YELLOW_BOLD	"\033[1;33m"
#define FG_BLUE     	"\033[0;34m"  /* 9 -> strike ; 34 -> blue */
#define FG_BLUE_BOLD    "\033[1;34m"
#define FG_CYAN     	"\033[0;36m" /* 0 -> normal ; 36 -> cyan */
#define FG_CYAN_BOLD	"\033[0;36m"
#define FG_DEFAULT  "\033[39m"
#define BG_RED      "\033[41m"
#define BG_GREEN    "\033[42m"
#define BG_BLUE     "\033[44m"
#define BG_DEFAULT  "\033[49m"
#define RESET_COLOR    "\033[0m" /* to flush the previous property */

#define LOG_INFO (1<<0) //1
#define LOG_WARN (1<<1) //2
#define LOG_ERROR (1<<2) //4
#define LOG_DEBUG (1<<3) //8
#define LOG_ALL ((2<<16)-1)

#define INFO(f...)  do { LOG_WRITER(LOG_INFO, __FILE__, __FUNCTION__, __LINE__, f); } while (0)
#define WARN(f...)  do { LOG_WRITER(LOG_WARN, __FILE__, __FUNCTION__, __LINE__, f); } while (0)
#define ERROR(f...) do { LOG_WRITER(LOG_ERROR, __FILE__, __FUNCTION__, __LINE__, f); } while (0)
#define DEBUG(f...) do { LOG_WRITER(LOG_DEBUG, __FILE__, __FUNCTION__, __LINE__, f); } while (0)
#define DBG(l,f...) do { LOG_WRITER(l, __FILE__, __FUNCTION__, __LINE__, f); } while (0)

#define LOG_MAX_MSG_LEN 4098

static void *_20131025_log_device = NULL;
static int  _20131025_log_level = 1;

extern void LOG_INIT(const char *device);
const char* LOG_SET_COLOR(int level, int is_end);
extern void LOG_DESTORY();
extern void LOG_LEVEL();
static void LOG_ADD(int level, const char *file, const char * func, int line, const char *msg);
static void LOG_WRITER(int level, const char *file, const char * func, int line, const char *fmt, ...);

void LOG_INIT(const char *device)
{
	_20131025_log_device = !device ? stdout : fopen((const char *)device, "a+");
}
const char* LOG_SET_COLOR(int level, int is_end)
{
	if (_20131025_log_device == stdout) {
		if (is_end) {
			return RESET_COLOR;
		} else {
			switch (level) {
				case LOG_INFO : return FG_DEFAULT; break;
				case LOG_WARN : return FG_YELLOW; break;
				case LOG_ERROR : return FG_RED; break;
				case LOG_DEBUG : return FG_CYAN; break;
				default : return FG_BLUE_BOLD; break;
			}
		}
	}
	return "";
}

void LOG_DESTORY()
{
	if (_20131025_log_device != stdout) fclose(_20131025_log_device);
}

void LOG_LEVEL(int level)
{
	_20131025_log_level = level;
}


static void LOG_ADD(int level, const char *file, const char * func, int line, const char *msg)
{
    const char *c = "IWEDX";
    const char *datetime_format = "%Y-%m-%d %H:%M:%S";
    time_t meow = time( NULL );
    char buf[64];
    int cl = 0;

    if ((level & ~_20131025_log_level) != 0 ) return;
	//cl = floor(log(level)/log(2)) + 1;
	switch (level) {
		case LOG_INFO : cl = 0; break;
		case LOG_WARN : cl = 1; break;
		case LOG_ERROR : cl = 2; break;
		case LOG_DEBUG : cl = 3; break;
		default : cl = 4; break;
	}	
	
    strftime( buf, sizeof(buf), datetime_format, localtime(&meow) );
    fprintf(_20131025_log_device, "%s%s[%d][%s(%s):%d] %c, %s%s\n",LOG_SET_COLOR(level, 0) , buf, (int)getpid(), file, func, line, c[cl], msg ,LOG_SET_COLOR(level, 1));
}

static void LOG_WRITER(int level, const char *file, const char * func, int line, const char *fmt, ...)
{ 
    va_list ap;
    char msg[LOG_MAX_MSG_LEN];

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);  
    LOG_ADD(level, file, func, line, msg);    
    va_end(ap);
}

int main(int argc, char *argv[])
{
	int j;
	LOG_INIT(NULL);
	//LOG_INIT("a.log");
	LOG_LEVEL(1<<5);
    for ( j = 0; j < 2; j++ ) {
         INFO("INFO: %d", j);
         ERROR("ERROR: %d", j);
         DEBUG("DEBUG: %s", "dddd");   
         WARN("WARN: %d", j);      
         DBG(32, "DBG: %d", j);   
    }
    LOG_DESTORY();
	//sleep(1);  // you can ps now
    return 0;
}

