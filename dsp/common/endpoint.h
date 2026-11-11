#ifndef PCD_ENDPOINT_H
#define PCD_ENDPOINT_H
#include "protocol.h"
#include <string.h>
typedef struct {
    PcdCommand command;
    uint32_t last_update_us,rejected,watchdogs;
    uint16_t mask,fault;
    bool synchronized,active;
} PcdEndpoint;
static inline void pcd_stop(PcdEndpoint *e) {
    uint16_t i;e->mask=0;e->active=false;e->command.digital_out=0;
    for(i=0;i<PCD_AXES;i++)e->command.duty[i]=0;
    for(i=0;i<PCD_AO;i++)e->command.analog_out[i]=0;
}
static inline bool pcd_accept(PcdEndpoint *e,const PcdCommand *c,uint32_t now,uint16_t encoder_valid) {
    uint16_t i;
    /* STOP/SYNC may always remove drive, even with a foreign/stale session. */
    if(c->operation==PCD_DISARM)pcd_stop(e);
    if(c->operation==PCD_SYNC){
        pcd_stop(e);
        if(!c->session||(e->synchronized&&c->session==e->command.session&&
           (int32_t)(c->sequence-e->command.sequence)<=0)){e->rejected++;return false;}
        e->command=*c;pcd_stop(e);e->synchronized=true;e->last_update_us=now;return true;
    }
    if(!e->synchronized||c->session!=e->command.session||
       (int32_t)(c->sequence-e->command.sequence)<=0){e->rejected++;return false;}
    if(c->operation>PCD_CLEAR||c->arm_mask>63U||c->digital_out>15U||
       c->analog_out[0]>4095U||c->analog_out[1]>4095U)goto invalid;
    for(i=0;i<PCD_AXES;i++) if(!(c->duty[i]>=-1.0f&&c->duty[i]<=1.0f))goto invalid;
    if(c->operation==PCD_CLEAR){
        if(e->active)goto invalid;
        e->fault=0;e->command=*c;pcd_stop(e);
    } else if(c->operation==PCD_DISARM){e->command=*c;pcd_stop(e);}
    else if(e->fault){e->rejected++;return false;}
    else if(c->operation==PCD_ARM){
        if(c->arm_mask & ~encoder_valid)goto invalid;
        e->command=*c;pcd_stop(e);e->mask=c->arm_mask;e->active=true;
    } else {
        if(!e->active||c->arm_mask!=e->mask)goto invalid;
        e->command=*c;
    }
    e->last_update_us=now;return true;
invalid:
    e->rejected++;e->fault|=PCD_FAULT_COMMAND;pcd_stop(e);return false;
}
static inline void pcd_tick(PcdEndpoint *e,uint32_t now,uint16_t encoder_valid) {
    if(e->active&&(uint32_t)(now-e->last_update_us)>PCD_TIMEOUT_US){
        e->watchdogs++;e->fault|=PCD_FAULT_TIMEOUT;pcd_stop(e);
    }
    if(e->mask & ~encoder_valid){e->fault|=PCD_FAULT_ENCODER;pcd_stop(e);}
}
#endif
