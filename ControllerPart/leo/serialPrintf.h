#include <stdarg.h>
#define SERIAL_PRINTF_MAX_BUFF      256
#define F_PRECISION                   6
void serialPrintf(const char *fmt, ...);
/**
 * --------------------------------------------------------------
 * Perform simple printing of formatted data
 * Supported conversion specifiers: 
 *      d, i     signed int
 *      u        unsigned int
 *      ld, li   signed long
 *      lu       unsigned long
 *      f        double
 *      c        char
 *      s        string
 *      %        '%'
 * Usage: %[conversion specifier]
 * Note: This function does not support these format specifiers: 
 *      [flag][min width][precision][length modifier]
 * --------------------------------------------------------------
 */
void serialPrintf(const char *fmt, ...) {
  /* buffer for storing the formatted data */
  char buf[SERIAL_PRINTF_MAX_BUFF];
  char *pbuf = buf;
  /* pointer to the variable arguments list */
  va_list pargs;
  
  /* Initialise pargs to point to the first optional argument */
  va_start(pargs, fmt);
  /* Iterate through the formatted string to replace all 
  conversion specifiers with the respective values */
  while(*fmt) {
    if(*fmt == '%') {
      switch(*(++fmt)) {
        case 'd': 
        case 'i': 
          pbuf += sprintf(pbuf, "%d", va_arg(pargs, int));
          break;
        case 'u': 
          pbuf += sprintf(pbuf, "%u", va_arg(pargs, unsigned int));
          break;
        case 'l': 
          switch(*(++fmt)) {
            case 'd': 
            case 'i': 
              pbuf += sprintf(pbuf, "%ld", va_arg(pargs, long));
              break;
            case 'u': 
              pbuf += sprintf( pbuf, "%lu", 
                               va_arg(pargs, unsigned long));
              break;
          }
          break;
        case 'f': 
          pbuf += strlen(dtostrf( va_arg(pargs, double), 
                                  1, F_PRECISION, pbuf));
          break;
        
        case 'c':
          *(pbuf++) = (char)va_arg(pargs, int);
          break;
        case 's': 
          pbuf += sprintf(pbuf, "%s", va_arg(pargs, char *));
          break;
        case '%':
          *(pbuf++) = '%';
          break;
        default:
          break;
      }
    } else {
      *(pbuf++) = *fmt;
    }
    
    fmt++;
  }
  
  *pbuf = '\0';
  
  va_end(pargs);
  Serial.print(buf);
}