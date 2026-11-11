#ifndef PCD_PROTOCOL_H
#define PCD_PROTOCOL_H
/* Wire v5: explicit 16-bit words, never packed C structs. C28x char is 16 bits. */
#include <stdint.h>
#include <stdbool.h>
#define PCD_VERSION 5U
#define PCD_WORDS 96U
#define PCD_AXES 6U
#define PCD_AI 6U
#define PCD_AO 2U
#define PCD_COMMAND_HEADER 0x55AAU
#define PCD_TELEMETRY_HEADER 0xAA55U
#define PCD_TX_START 4U
#define PCD_TX_CRC 92U
#define PCD_RX_CRC 95U
#define PCD_DEVICE 0x2837U
#define PCD_FIRMWARE 0x0100U
#define PCD_CONFIG 1U
#define PCD_CAPABILITIES 0x000FU /* duty, encoders, DIO, AIO; no FOC */
#define PCD_DISARM 0U
#define PCD_ARM 1U
#define PCD_CYCLE 2U
#define PCD_CLEAR 3U
#define PCD_SYNC 4U
#define PCD_FAULT_TIMEOUT 1U
#define PCD_FAULT_COMMAND 2U
#define PCD_FAULT_ENCODER 4U
#define PCD_TIMEOUT_US 50000UL

typedef struct {
    uint16_t operation, arm_mask, digital_out;
    uint32_t session, sequence;
    uint16_t analog_out[PCD_AO];
    float duty[PCD_AXES];
    int32_t reference[PCD_AXES]; /* Pi reference counts, echoed, not a DSP setpoint */
} PcdCommand;
typedef struct {
    uint32_t timestamp_us, sequence, session, command_sequence;
    uint16_t state, arm_mask, fault, encoder_valid, digital_in, digital_out;
    uint16_t analog_valid, analog_out_valid;
    uint16_t analog_in[PCD_AI], analog_out[PCD_AO];
    int32_t position[PCD_AXES], reference[PCD_AXES];
    float requested_duty[PCD_AXES], duty[PCD_AXES];
    uint32_t invalid_frames, rejected_commands, watchdogs, encoder_errors;
    uint16_t cpu2_max_poll_us;
} PcdTelemetry;

static inline uint32_t pcd_get32(const uint16_t *p) {return (uint32_t)p[0] | ((uint32_t)p[1]<<16);}
static inline void pcd_put32(uint16_t *p,uint32_t v) {p[0]=(uint16_t)v;p[1]=(uint16_t)(v>>16);}
static inline float pcd_get_float(const uint16_t *p) {union {float f;uint32_t u;} v;v.u=pcd_get32(p);return v.f;}
static inline void pcd_put_float(uint16_t *p,float f) {union {float f;uint32_t u;} v;v.f=f;pcd_put32(p,v.u);}
static inline uint16_t pcd_crc(const uint16_t *p,uint16_t count) {
    /* Nibble table keeps CRC bounded without a 256-entry flash table. */
    static const uint16_t table[16]={0x0000,0x1021,0x2042,0x3063,0x4084,0x50a5,0x60c6,0x70e7,
        0x8108,0x9129,0xa14a,0xb16b,0xc18c,0xd1ad,0xe1ce,0xf1ef};
    uint16_t crc=0xffff,i,n;
    for(i=0;i<count;i++) for(n=0;n<4;n++)
        crc=(uint16_t)((crc<<4)^table[((crc>>12)^(p[i]>>(12-4*n)))&15U]);
    return crc;
}
static inline void pcd_encode_command(uint16_t *w,const PcdCommand *c) {
    uint16_t i;for(i=0;i<PCD_WORDS;i++) w[i]=0;
    w[0]=PCD_COMMAND_HEADER;w[1]=PCD_VERSION;w[2]=PCD_WORDS;w[3]=c->operation;
    pcd_put32(w+4,c->session);pcd_put32(w+6,c->sequence);
    w[8]=c->arm_mask;w[9]=c->digital_out;w[10]=c->analog_out[0];w[11]=c->analog_out[1];
    for(i=0;i<PCD_AXES;i++){pcd_put_float(w+12+2*i,c->duty[i]);pcd_put32(w+24+2*i,(uint32_t)c->reference[i]);}
    w[PCD_RX_CRC]=pcd_crc(w,PCD_RX_CRC);
}
static inline bool pcd_decode_command(const uint16_t *w,PcdCommand *c) {
    uint16_t i;
    if(w[0]!=PCD_COMMAND_HEADER||w[1]!=PCD_VERSION||w[2]!=PCD_WORDS||pcd_crc(w,PCD_RX_CRC)!=w[PCD_RX_CRC])return false;
    for(i=36;i<PCD_RX_CRC;i++) if(w[i])return false;
    c->operation=w[3];c->session=pcd_get32(w+4);c->sequence=pcd_get32(w+6);
    c->arm_mask=w[8];c->digital_out=w[9];c->analog_out[0]=w[10];c->analog_out[1]=w[11];
    for(i=0;i<PCD_AXES;i++){c->duty[i]=pcd_get_float(w+12+2*i);c->reference[i]=(int32_t)pcd_get32(w+24+2*i);}
    return true;
}
static inline void pcd_encode_telemetry(uint16_t *w,const PcdTelemetry *t) {
    uint16_t i;for(i=0;i<PCD_WORDS;i++)w[i]=0;
    for(i=0;i<PCD_TX_START;i++)w[i]=0xffff;
    w[4]=PCD_TELEMETRY_HEADER;w[5]=PCD_VERSION;w[6]=PCD_WORDS;w[7]=PCD_DEVICE;
    w[8]=PCD_FIRMWARE;w[9]=PCD_CAPABILITIES;
    pcd_put32(w+10,t->timestamp_us);pcd_put32(w+12,t->sequence);pcd_put32(w+14,t->session);pcd_put32(w+16,t->command_sequence);
    w[18]=t->state;w[19]=t->arm_mask;w[20]=t->fault;w[21]=t->encoder_valid;
    w[22]=t->digital_in;w[23]=t->digital_out;w[24]=t->analog_valid;w[25]=t->analog_out_valid;
    for(i=0;i<PCD_AI;i++)w[26+i]=t->analog_in[i];
    for(i=0;i<PCD_AO;i++)w[32+i]=t->analog_out[i];
    for(i=0;i<PCD_AXES;i++){
        pcd_put32(w+34+2*i,(uint32_t)t->position[i]);pcd_put_float(w+46+2*i,t->requested_duty[i]);
        pcd_put_float(w+58+2*i,t->duty[i]);pcd_put32(w+70+2*i,(uint32_t)t->reference[i]);
    }
    pcd_put32(w+82,t->invalid_frames);pcd_put32(w+84,t->rejected_commands);
    pcd_put32(w+86,t->watchdogs);pcd_put32(w+88,t->encoder_errors);
    w[90]=PCD_CONFIG;w[91]=t->cpu2_max_poll_us;
    w[PCD_TX_CRC]=pcd_crc(w+PCD_TX_START,PCD_TX_CRC-PCD_TX_START);
}
static inline bool pcd_decode_telemetry(const uint16_t *w,PcdTelemetry *t) {
    uint16_t i; /* Caller aligns the header at word 4 after pipeline search. */
    if(w[4]!=PCD_TELEMETRY_HEADER||w[5]!=PCD_VERSION||w[6]!=PCD_WORDS||w[7]!=PCD_DEVICE||w[90]!=PCD_CONFIG||
       pcd_crc(w+4,PCD_TX_CRC-4)!=w[PCD_TX_CRC])return false;
    t->timestamp_us=pcd_get32(w+10);t->sequence=pcd_get32(w+12);t->session=pcd_get32(w+14);t->command_sequence=pcd_get32(w+16);
    t->state=w[18];t->arm_mask=w[19];t->fault=w[20];t->encoder_valid=w[21];
    t->digital_in=w[22];t->digital_out=w[23];t->analog_valid=w[24];t->analog_out_valid=w[25];
    for(i=0;i<PCD_AI;i++)t->analog_in[i]=w[26+i];
    for(i=0;i<PCD_AO;i++)t->analog_out[i]=w[32+i];
    for(i=0;i<PCD_AXES;i++){
        t->position[i]=(int32_t)pcd_get32(w+34+2*i);t->requested_duty[i]=pcd_get_float(w+46+2*i);
        t->duty[i]=pcd_get_float(w+58+2*i);t->reference[i]=(int32_t)pcd_get32(w+70+2*i);
    }
    t->invalid_frames=pcd_get32(w+82);t->rejected_commands=pcd_get32(w+84);
    t->watchdogs=pcd_get32(w+86);t->encoder_errors=pcd_get32(w+88);t->cpu2_max_poll_us=w[91];return true;
}
#endif
