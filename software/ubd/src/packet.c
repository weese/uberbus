#include <stdint.h>
#include <glib.h>
#include <string.h>
#include <stdio.h>

#include "packet.h"
#include "ubpacket.h"
#include "message.h"


//struct queues packet_queues;
void(*packet_outmessage)(struct message* msg);

GHashTable* packet_callbacks;

void blubb (gpointer key, gpointer value, gpointer data)
{
    value = NULL;
    data = NULL;
    printf(key);
}
void packet_inpacket(struct ubpacket* p)
{
    void(*cb)(struct ubpacket*);
    gchar buf[2];
    buf[0] = p->data[0];
    buf[1] = 0;
/*    printf("registred:\n");
    g_hash_table_foreach(packet_callbacks,blubb,NULL);
    printf("end\n");*/
    cb = g_hash_table_lookup(packet_callbacks, buf);
    if( cb ){
        cb(p);
    }else{
        printf("There is no handler registerd for packet type %s\n",buf);
    }
}

void packet_init(//void(*inpacket)(struct ubpacket*),
                   void(*outmsg)(struct message* msg))
{
    packet_callbacks = g_hash_table_new(g_str_hash, g_str_equal); 
    //packet_queues.packet_in = g_async_queue_new(); 
    //packet_queues.status_in = g_async_queue_new();

//    packet_inpacket = inpacket;
    packet_outmessage = outmsg;
}

void packet_addCallback(gchar key, void(*cb)(struct ubpacket*))
{
    gchar buf[2];
    buf[0] = key;
    buf[1] = 0;

    g_hash_table_insert(packet_callbacks, g_strdup(buf), cb);

    printf("Added callback for message type %c to 0x%x\n",key,(unsigned int)cb);
}

char busy = 0;
void packet_inmessage(struct message* msg)
{
    if( msg->data[0] == 'P' ){
        struct ubpacket * p = g_new(struct ubpacket,1);
        memcpy(p,msg->data+1,msg->len-1);
        //g_async_queue_push(packet_queues.packet_in,p);
        g_free(msg);
        packet_inpacket(p);
    }else if( msg->data[0] == 'S'){ 
        printf("got send done\n");
        busy = 0;
        //g_async_queue_push(packet_queues.status_in,msg);
    }else if( msg->data[0] == 'A'){
        printf("got abort\n");
        busy = 0;
    }
}

void packet_outpacket(struct ubpacket* p)
{
    struct message outmsg;
    if(busy)
        return;
    //    while(1);
    p->src = 1;
    p->flags = 0;
    outmsg.len = p->len+UB_PACKET_HEADER;
    //outmsg.data[0] = 'P';
    printf("sending packet with dest=%u src=%u flags=%u len=%u\n",p->dest, p->src, p->flags,p->len);
    memcpy(outmsg.data, p, p->len + UB_PACKET_HEADER);
    busy = 1;
    packet_outmessage(&outmsg);
}
