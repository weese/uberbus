#include <string.h>
#include <avr/interrupt.h>

#include "ubconfig.h"
#include "ubrs485master.h"
#include "ubstat.h"
#include "ubrs485uart.h"
#include "settings.h"
#include "ubtimer.h"
#include "ub.h"
#include "ubrs485message.h"
#include "udebug.h"
#include "ubpacket.h"
#include "ubcrc.h"
#include "ubaddress.h"
#include "udebug.h"

#define UB_TICKSPERBYTE             10
#define UB_INITIALDISCOVERINTERVAL  100

#define UB_QUERYINTERVALMIN      64      //in ms
#define UB_QUERYINTERVALMAX      1000    //in ms

#define UB_QUERYINTERVALMAXCOUNT    (UB_QUERYINTERVALMAX/UB_QUERYINTERVALMIN)

#define RS485M_STATE_INIT               1
#define RS485M_STATE_INITIALDISCOVER    2
#define RS485M_STATE_STOP               5
#define RS485M_NORMAL                   3
#define RS485M_DISCOVER                 4

#define RS485M_SLOTCOUNT                3
#define RS485M_QUERYSLOT                0
#define RS485M_DISCOVERSLOT             1
#define RS485M_PACKETSLOT               2

#define RS485M_BUS_IDLE              0
#define RS485M_BUS_SEND_START        1
#define RS485M_BUS_SEND_DATA         2
#define RS485M_BUS_SEND_STOP         3
#define RS485M_BUS_SEND_STOP2        4
#define RS485M_BUS_SEND_DONE         5
#define RS485M_BUS_SEND_TIMER        6
#define RS485M_BUS_RECV              7
#define RS485M_BUS_SEND_TIMER_WAIT   8

struct rs485m_slot {
    uint8_t     adr;
    uint8_t     state;
    uint8_t     *data;
    uint8_t     len;
    uint8_t     full;
};


struct rs485m_slot rs485m_slots[RS485M_SLOTCOUNT];

uint8_t    rs485m_state = RS485M_STATE_INIT;
uint16_t   rs485m_ticks;


//used in the interrupts
volatile uint8_t rs485m_start;
volatile uint8_t rs485m_len;
volatile uint8_t *rs485m_data;
volatile uint8_t rs485m_stop;
volatile uint8_t rs485m_timer;

volatile uint8_t rs485m_busState;
volatile uint8_t rs485m_granted;

volatile uint8_t rs485m_incomming;
volatile uint8_t rs485m_packetdata[UB_PACKETLEN+2];    //+ 2 byte crc

//buffer for the slave id during a query
uint8_t rs485m_querybuf[1];
volatile uint8_t rs485m_stalled;

UBSTATUS rs485master_setQueryInterval(uint8_t adr, uint16_t interval)
{ 
    //Check for correct intervals
    if( interval < UB_QUERYINTERVALMIN )
        interval = UB_QUERYINTERVALMIN;
    if( interval > UB_QUERYINTERVALMAX )
        interval = UB_QUERYINTERVALMAX;
    
    interval = interval / UB_QUERYINTERVALMIN;

    struct ubstat_t * flags = ubstat_getFlags(adr);
    flags->interval = interval;
    flags->counter  = interval;
    return UB_OK;
}

void rs485master_init(void)
{
    uint8_t i;
    ubtimer_init();
    rs485uart_init(UART_BAUD_SELECT(RS485_BITRATE,F_CPU));

    for(i=0; i<RS485M_SLOTCOUNT; i++){
        rs485m_slots[i].full = 0;
    }

    rs485m_busState = RS485M_BUS_IDLE;
    rs485m_incomming = UB_NONE;
    rs485m_granted = 0;
    rs485m_stalled = 0;
    rs485m_ticks = 0;
    rs485m_state = RS485M_STATE_INIT;

    rs485uart_enableReceive();
}

int16_t rs485master_getPacket(struct ubpacket_t * packet)
{
    int16_t len = 0;
    cli();
    //we don't want the value to change while testing it
    uint8_t incomming = rs485m_incomming;
    sei();

    if( incomming == UB_START ){

        len = rs485msg_getLen();
        uint8_t * msg = rs485msg_getMsg();

        uint16_t crc = ubcrc16_data(msg, len - 2);
        if( (crc>>8) == msg[len-2] && (crc&0xFF) == msg[len-1] ){
            memcpy(packet, msg , len);
            len-=2;
            uint8_t adr = packet->header.dest;
            if( !ubadr_isMulticast(adr) &&
                !ubadr_isBroadcast(adr) &&
                !(packet->header.flags & UB_PACKET_ACK) &&
                !(packet->header.flags & UB_PACKET_NOACK)){
                PORTA |= (1<<PA7);
                rs485m_stalled = 1;
            }
        }else{
            UDEBUG("Dcrcerror");
            len = 0;
        }
        rs485m_incomming = UB_NONE;
    }else if( incomming != UB_NONE ){
        //ignore these packages
        rs485m_incomming = UB_NONE;
    }
    //return len;
    return len?UB_OK:UB_ERROR;
}

//try to send a query to a node
UBSTATUS rs485master_query(uint8_t adr)
{
    if( rs485m_slots[RS485M_QUERYSLOT].full )
        return UB_ERROR;
    rs485m_slots[RS485M_QUERYSLOT].adr = adr;
    rs485m_slots[RS485M_QUERYSLOT].full = 1;
    return UB_OK;
}

UBSTATUS rs485master_discover(void)
{
    if( rs485m_slots[RS485M_DISCOVERSLOT].full )
        return UB_ERROR;
    rs485m_slots[RS485M_DISCOVERSLOT].full = 1;
    return UB_OK;
}
/*
 *  Query the next node.
 *  We don't want do check every node during each run. So only only the
 *  minimum number of nodes is checkes in each interval.
 */
void rs485master_querynodes(void)
{
    uint8_t i;

    //decrement every node during one minimum interval if it is not to be
    //checked already
    static uint8_t counterindex = 0;
    for(i=0; i<(UB_NODEMAX/UB_QUERYINTERVALMIN); i++){
        struct ubstat_t * flags = ubstat_getFlags(counterindex++);
        if( flags->counter )
            flags->counter--;
        if( counterindex == UB_NODEMAX )
            counterindex = 0;
    }

    //check the nodes with interval == 0
    //if we have to check to often this will limit the query interval
    //to the maximum of 1000 queries per second!
    static uint8_t queryindex = 0;
    for(i=0; i<(UB_NODEMAX/UB_QUERYINTERVALMIN); i++){
        uint8_t adr = queryindex;
        struct ubstat_t * flags = ubstat_getFlags(queryindex++);
        if( queryindex == UB_NODEMAX )
            queryindex = 0;
        //TODO: maybe this uses too much cpu time
        if( flags->known && flags->counter == 0){
            if( rs485master_query(adr) == UB_OK ){
                flags->counter = flags->interval;
            }else{
                //the query failed. check again next time
                queryindex = adr;
            }
            //we do only one check per ms
            break;
        }
    }
}

//send a frame with data.
UBSTATUS rs485master_sendPacket(struct ubpacket_t * packet)
{
    uint8_t len = packet->header.len + sizeof(packet->header);

    //the frame has to contain data
    //if( len == 0 )
    //    return UB_ERROR;

    //use the free packet slot
    if( rs485m_slots[RS485M_PACKETSLOT].full ){
        UDEBUG("Drs485merror");
        return UB_ERROR;
    }

    //TODO: check for overflow!
    memcpy((char*)rs485m_packetdata, packet, len);

    uint16_t crc = ubcrc16_data((uint8_t*)rs485m_packetdata, len);
    rs485m_packetdata[len] = crc>>8;
    rs485m_packetdata[len+1] = crc&0xFF;

    rs485m_slots[RS485M_PACKETSLOT].data = (uint8_t*)rs485m_packetdata;
    rs485m_slots[RS485M_PACKETSLOT].len = len+2;
    rs485m_slots[RS485M_PACKETSLOT].full = 1;
    return UB_OK;
}

//1ms
inline void rs485master_tick(void)
{
    rs485m_ticks++;
    rs485master_querynodes();
    if( rs485m_granted )
        rs485m_granted--;
}

UBSTATUS rs485master_free(void)
{
    if( rs485m_slots[RS485M_PACKETSLOT].full )
        return UB_ERROR;
    return UB_OK;
}

UBSTATUS rs485master_idle(void)
{
    if( rs485m_busState == RS485M_BUS_IDLE )
        return UB_OK;
    return UB_ERROR;
}

inline void rs485master_process(void)
{
    switch( rs485m_state ){
        case RS485M_STATE_INIT:
            rs485m_state = RS485M_STATE_INITIALDISCOVER;
        break;
        case RS485M_STATE_INITIALDISCOVER:
            if( rs485m_ticks == UB_INITIALDISCOVERINTERVAL ){
                rs485m_ticks = 0;
                rs485master_discover();
            }
        break;
    }

    //wait for the bus to be idle and that all packets are processed
    //don't get a new packet when we still have to process one.
    cli();  //make the if atom
    if( rs485master_idle() == UB_OK  && rs485m_incomming == UB_NONE ){
        rs485master_runslot();
    }
    sei();
}

void rs485master_runslot(void)
{
    //check if a slave has the bus
    if( rs485m_granted )
        return;
    //implement priorities by aranging these items
    if( rs485m_slots[RS485M_DISCOVERSLOT].full && !rs485m_stalled){
        //just send the discover escape sequence
        //and start the receive windwo
        rs485master_start(UB_DISCOVER,NULL,0,0);
        rs485m_slots[RS485M_DISCOVERSLOT].full = 0;
        rs485m_timer = 4 * UB_TICKSPERBYTE;
    }else if( rs485m_slots[RS485M_QUERYSLOT].full && !rs485m_stalled){
        //send the query escape followed by the slave id

        //buffer the slave id, the slot might get overwritten
        rs485m_querybuf[0] = rs485m_slots[RS485M_QUERYSLOT].adr;
        rs485master_start(UB_QUERY,rs485m_querybuf,1,0);
        rs485m_slots[RS485M_QUERYSLOT].full = 0;
        rs485m_timer = 4 * UB_TICKSPERBYTE;
    }else if( rs485m_slots[RS485M_PACKETSLOT].full ){
        rs485master_start(UB_START,rs485m_slots[RS485M_PACKETSLOT].data,
                rs485m_slots[RS485M_PACKETSLOT].len,UB_STOP);
        //this slot will be freed when UB_STOP has been transmitted
        PORTA &= ~(1<<PA7);
        rs485m_stalled = 0;
    }
}

void rs485master_start(uint8_t start, uint8_t * data, uint8_t len, uint8_t stop)
{
    rs485uart_enableTransmit();
    rs485m_busState = RS485M_BUS_SEND_START;
    rs485m_start = start;
    rs485m_stop = stop;
    rs485m_data = data;
    rs485m_len = len;
    rs485uart_putc(UB_ESCAPE);
}

//rx interrupt
inline void rs485master_rx(void)
{
    //udebug_rx();
    uint16_t i = rs485uart_getc();
    if( !(i & UART_NO_DATA) ){
        //restart timeout
        ubtimer_start(2 * UB_TICKSPERBYTE);
        uint8_t c = i&0xFF;

        i = rs485msg_put(c);
        if( i != UB_NONE ){
            rs485m_incomming = i;
            ubtimer_stop();
            rs485m_busState = RS485M_BUS_IDLE;
            if( i == UB_START ){
            }
        }
    }
}

inline void rs485master_edge(void)
{
    if( rs485m_busState == RS485M_BUS_SEND_TIMER ){
        if( rs485uart_lineActive() && rs485uart_isReceiving() ){
            rs485m_busState = RS485M_BUS_RECV;
            rs485uart_edgeDisable();
            //return to RS485M_BUS_IDLE if nothing gets received(noise)
            ubtimer_start(2 * UB_TICKSPERBYTE);
        }else{
            //this was not a valid start
        }
    }else{
        //we can't start receiving
        rs485uart_edgeDisable();
    }
}

inline void rs485master_timer(void)
{
    //is this timeout still waiting for a response?
    if( rs485m_busState ==  RS485M_BUS_SEND_TIMER || rs485m_busState == RS485M_BUS_RECV ){
        //proceed to the next slot
        rs485m_busState = RS485M_BUS_IDLE;
    }
    rs485uart_edgeDisable();
    ubtimer_stop();
}

inline void rs485master_setTimeout(void)
{
    //wait for 4 bytes before timeout
    ubtimer_start(rs485m_timer);
    rs485m_busState = RS485M_BUS_SEND_TIMER;
    rs485uart_edgeEnable();
}

inline void rs485master_tx(void)
{
    static uint8_t pos;
    static uint8_t escaped;
    uint8_t data;

    switch(rs485m_busState){
        case RS485M_BUS_SEND_START:
            rs485uart_putc(rs485m_start);
            pos = 0;
            
            if( rs485m_len != 0)
                rs485m_busState = RS485M_BUS_SEND_DATA;
            else if( rs485m_stop != 0 )
                rs485m_busState = RS485M_BUS_SEND_STOP;
            else
                rs485m_busState = RS485M_BUS_SEND_TIMER_WAIT;
        break;
        case RS485M_BUS_SEND_DATA:
            data = rs485m_data[pos];
            if( data == UB_ESCAPE && !escaped){
                //insert an escape into the datastream
                rs485uart_putc(UB_ESCAPE);
                escaped = 1;
            }else{
                escaped = 0;
                rs485uart_putc(data);
                if( ++pos == rs485m_len ){
                    if( rs485m_stop == 0 ){
                        //no stop specified.
                        //give the slave time to answer
                        rs485m_busState = RS485M_BUS_SEND_TIMER_WAIT;
                    }else{
                        //only reached if a packet with data was transmitted
                        rs485m_slots[RS485M_PACKETSLOT].full = 0;
                        rs485m_busState = RS485M_BUS_SEND_STOP;
                    }
                }
            }
        break;
        case RS485M_BUS_SEND_STOP:
            rs485uart_putc(UB_ESCAPE);
            rs485m_busState = RS485M_BUS_SEND_STOP2;
        break;
        case RS485M_BUS_SEND_STOP2:
             rs485uart_putc(rs485m_stop);
             rs485m_busState = RS485M_BUS_SEND_DONE;
        break;
        case RS485M_BUS_IDLE:
        default:
        break;
    }
}

inline void rs485master_txend(void)
{
    if( rs485m_busState == RS485M_BUS_SEND_TIMER_WAIT )
        rs485master_setTimeout();
    if( rs485m_busState == RS485M_BUS_SEND_DONE ){
        rs485m_busState = RS485M_BUS_IDLE;
        rs485m_granted = 10;
    }
    if( rs485m_busState == RS485M_BUS_IDLE
            || rs485m_busState == RS485M_BUS_SEND_TIMER){
        rs485uart_enableReceive();
    }
}


