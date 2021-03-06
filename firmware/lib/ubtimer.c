    #include <avr/io.h>
#include <avr/interrupt.h>

#include "ub.h"
#include "ubconfig.h"
#include "ubtimer.h"
#include "ubrs485master.h"
#include "ubrs485slave.h"

#define UBTIMER_PRESCALER       32
#define UBTIMER_TICKSPERBIT     (F_CPU/RS485_BITRATE/UBTIMER_PRESCALER)

ISR(TIMER2_COMPA_vect)//, ISR_NOBLOCK)
{
#ifdef UB_ENABLEBRIDGE
#ifdef UB_ENABLERS485
    if( ubconfig.rs485master ){
        rs485master_timer();
    }
#endif
#endif

#ifdef UB_ENABLESLAVE
#ifdef UB_ENABLERS485
    if( ubconfig.rs485slave ){
        rs485slave_timer();
    }
#endif
#endif
}

void ubtimer_init(void)
{
    TCCR2A = (1<<WGM21);        //CTC
    TCCR2B = 0;
    TIMSK2 = (1<<OCIE2A);
}

//generate an interrupt in t bittimes
void ubtimer_start(uint8_t t)
{
    TCNT2 = 0;
    OCR2A = UBTIMER_TICKSPERBIT * t;
    //OCR2A = 16;
    TCCR2B = (1<<CS21) | (1<<CS20);         // /32
}


void ubtimer_stop(void)
{ 
    TCCR2B = 0;         //stop
}

