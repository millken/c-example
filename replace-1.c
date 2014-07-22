#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>

#include <sys/timeb.h>

//http://creativeandcritical.net/str-replace-c/
char *replace_str(const char *str, const char *old, const char *new)
{
  char *ret, *r;
  const char *p, *q;
  size_t oldlen = strlen(old);
  size_t count, retlen, newlen = strlen(new);

  if (oldlen != newlen) {
    for (count = 0, p = str; (q = strstr(p, old)) != NULL; p = q + oldlen)
      count++;
    /* this is undefined if p - str > PTRDIFF_MAX */
    retlen = p - str + strlen(p) + count * (newlen - oldlen);
  } else
    retlen = strlen(str);

  if ((ret = malloc(retlen + 1)) == NULL)
    return NULL;

  for (r = ret, p = str; (q = strstr(p, old)) != NULL; p = q + oldlen) {
    /* this is undefined if q - p > PTRDIFF_MAX */
    ptrdiff_t l = q - p;
    memcpy(r, p, l);
    r += l;
    memcpy(r, new, newlen);
    r += newlen;
  }
  strcpy(r, p);

  return ret;
}

int random_int(int min, int max)
{
    /* Seed number for rand() */
    struct timeb t;
    ftime(&t);
    srand((unsigned int) 1000 * t.time + t.millitm + random());
	//srandom( time(0)+clock()+random() );
	unsigned int s_seed = 214013 * rand() + 2531011;
	return min+(s_seed ^ s_seed>>15)%(max-min+1);
}

//http://www.cnblogs.com/chenyuming507950417/archive/2012/01/02/2310114.html
char *replace_ip(const char *str, const char *old)
{
  char *ret, *r;
  const char *p, *q;
  size_t oldlen = strlen(old);
  size_t count, retlen, newlen = 4;
  
  char ip[3];
	for (count = 0, p = str; (q = strstr(p, old)) != NULL; p = q + oldlen)
	  count++;
	/* this is undefined if p - str > PTRDIFF_MAX */
	retlen = p - str + strlen(p) + count * (newlen - oldlen);  
  if ((ret = malloc(retlen + 1)) == NULL)
    return NULL;

  for (r = ret, p = str; (q = strstr(p, old)) != NULL; p = q + oldlen) {
    /* this is undefined if q - p > PTRDIFF_MAX */
    ptrdiff_t l = q - p;
    memcpy(r, p, l);
    r += l;
    bzero(ip, newlen);
    sprintf(ip, "%d", random_int(1, 255));
    newlen = strlen(ip);
    printf("%s=%d\n", ip, newlen);
    memcpy(r, ip, newlen);
    r += newlen;
  }
  strcpy(r, p);  
 return ret;  
}

char *
random_chars(char *dst, int start, int end)
{
	srandom( time(0)+clock()+random() ); //生成更好的随机数
    static const char allowable_chars[] = "1234567890abcdefhijklnmopqrstuwxyz";
    int i, r;
    int size = rand()%(end - start + 1) + start;/*n为a~b之间的随机数*/
    for (i = 0; i< size; i++) {
        r = (int)((double)rand() / ((double)RAND_MAX + 1) * (sizeof(allowable_chars) -1 ));
        dst[i] = allowable_chars[r];
    }
    dst[i] = '\0';

    return dst;
}

char *
replace_domain(const char *str, const char *old)
{
  char *ret, *r;
  const char *p, *q;
  size_t oldlen = strlen(old);
  size_t count, retlen, newlen = 26;
  
  char s[25];
  for (count = 0, p = str; (q = strstr(p, old)) != NULL; p = q + oldlen)
    count++;
  /* this is undefined if p - str > PTRDIFF_MAX */
  retlen = p - str + strlen(p) + count * (newlen - oldlen);  
  if ((ret = malloc(retlen + 1)) == NULL)
    return NULL;

  for (r = ret, p = str; (q = strstr(p, old)) != NULL; p = q + oldlen) {
    /* this is undefined if q - p > PTRDIFF_MAX */
    ptrdiff_t l = q - p;
    memcpy(r, p, l);
    r += l;
    bzero(s, newlen);
    random_chars(s, 5, 25);
    newlen = strlen(s);
    printf("%s=%d\n", s, newlen);
    memcpy(r, s, newlen);
    r += newlen;
  }
  strcpy(r, p);  
 return ret;  
}

int main(int argc, char **argv)
{
	char *s1, *s2, *s3, *d1, *d2;
	s1 = "1.*.33.*";
	d1 = "*.*.com";
	printf("%d\n", random_int(1, 300));
	
	s2 = replace_str(s1, "*", "23");
	printf("s2=%s\n", s2);
	
	s3 = replace_ip(s1, "*");
	printf("s3=%s\n", s3);	
	
	d2 = replace_domain(d1, "*");
	printf("d2=%s\n", d2);		
}
