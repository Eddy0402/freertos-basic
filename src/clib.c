#include "fio.h"
#include <stdarg.h>
#include "clib.h"

void send_byte(char );

size_t fio_printf(int fd, const char *format, ...){
    va_list(args);
    va_start(args, format);
    char dest[128];
    int count = vsnprintf(dest, 128, format, args);
    fio_write(fd, dest, count);
    va_end(args);
    return count;
}

int sprintf(char *dest, const char *format, ...){
     va_list args;
     va_start(args, format);
     int count = vsprintf(dest, format, args);
     va_end(args);
     return count;
}

int snprintf(char * dest, size_t n, const char * format, ...){
    va_list args;
    va_start(args, format);
    int count = vsnprintf(dest, n, format, args);
    va_end(args);
    return count;
    return 0;
}

size_t strlen(const char *str){
    size_t count;
    for(count=0;*str;++count, ++str);
    return count;
}

char *strcat(char * restrict dest, const char * restrict source){
    /* locate '\0' in dest */
    for(;*dest;++dest);
    /* copy character from source */
    for(;*source; ++dest, ++source)
        *dest=*source;
    *dest='\0';
    return dest;
}

char *itoa(const char *numbox, int num, unsigned int base){
    static char buf[32]={0};
    int i;
    if(num==0){
        buf[30]='0';
        return &buf[30];
    }
    int negative=(num<0);
    if(negative) num=-num;
    for(i=30; i>=0&&num; --i, num/=base)
        buf[i] = numbox[num % base];
    if(negative){
        buf[i]='-';
        --i;
    }
    return buf+i+1;
}

char *utoa(const char *numbox, unsigned int num, unsigned int base){
    static char buf[32]={0};
    int i;
    if(num==0){
        buf[30]='0';
        return &buf[30];
    }
    for(i=30; i>=0&&num; --i, num/=base)
        buf[i] = numbox [num % base];
    return buf+i+1;
}
