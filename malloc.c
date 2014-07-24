#include <stdio.h>
#include <stdlib.h>



void *
debug_malloc(size_t size, const char *file, int line, const char *func)
{
        void *p;

        p = malloc(size);
        printf("%s:%d:%s:malloc(%ld): p=0x%lx\n",
            file, line, func, size, (unsigned long)p);
        return p;
}

#define malloc(s) debug_malloc(s, __FILE__, __LINE__, __func__)
#define free(p)  do {                                                   \
        printf("%s:%d:%s:free(0x%lx)\n", __FILE__, __LINE__,            \
            __func__, (unsigned long)p);                                \
        free(p);                                                        \
} while (0)

int
main(int argc, char *argv[])
{
        char *p;
        p = malloc(1024);
        free(p);
        return 0;
}
