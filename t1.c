#include <stdio.h>
#include<string.h>
int main()
{
    char str[] = "1.1.1.1";
    char aa[10][16] = {0};
    char *p;
    char *buff;
    buff=str;
    p = strsep(&buff, ",");
    int i = 0;
    while(p)
    {
    	strcpy(aa[i] , p);
        printf("%s\n", p);
        p = strsep(&buff, ",");
        ++i;
    }
    for (i = 0; i < 10; ++i)
    {
    	if(strlen(aa[i]) >= 7)
    	printf("aa[%d]=%s\n", i, aa[i] );
    }
     for (i = 0; i < 10; ++i)
    {
    	if(strlen(aa[i]) >= 7)
    	printf("aa[%d]=%s\n", i, aa[i] );
    }   
    return 0;
}
