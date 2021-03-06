#ifndef __SERIAL_HANDLER_
#define __SERIAL_HANDLER_

#include <stdint.h>
#include <avr/io.h>
#include "ubconfig.h"
#include "usart.h"

#define SERIAL_BUFFERLEN        UB_PACKETLEN

#define SERIAL_ESCAPE   '\\'
#define SERIAL_START    '1'
#define SERIAL_END    '2'

//#define DEBUG(...) {serial_putStart(); serial_putcenc('D'); printf(__VA_ARGS__);serial_putStop();}

inline void serial_putcenc(uint8_t c);
void serial_putsenc(char * s);
inline void serial_putStart(void);
inline void serial_putStop(void);
#ifdef USEDEBUG
void serial_sendFrames(char * s);
#else
#define serial_sendFrames(x) {}
#endif
void serial_sendFramec(uint8_t s);
void serial_putenc(uint8_t * d, uint16_t n);
uint16_t readline( void );
uint16_t serial_readline(uint8_t * buf, uint8_t max);
#endif
