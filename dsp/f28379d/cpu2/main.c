#include "driverlib.h"
#include "device.h"
#include "ipc_layout.h"
#include "../../common/quadrature.h"
#include <string.h>

static volatile Cpu2ToCpu1Ipc *const shared=(volatile Cpu2ToCpu1Ipc *)0x03f800UL;
static volatile uint16_t *const ready=(volatile uint16_t *)0x03fc00UL;
static uint16_t previous[3];
static uint32_t counts[3],errors[3],sequence;
volatile uint32_t gMaxPollCycles,gPolls;
/* CPU2 has no other workload. Poll all six input pins together; unlike six
 * edge ISRs this has a measurable fixed maximum gap independent of edge rate. */
#pragma CODE_SECTION(poll,".TI.ramfunc")
static void poll(void) {
    uint32_t a=GPIO_readPortData(GPIO_PORT_A),b=GPIO_readPortData(GPIO_PORT_B);
    uint16_t states[3],i;int16_t delta;
    states[0]=(uint16_t)((a>>24)&3U);states[1]=(uint16_t)((a>>26)&3U);
    states[2]=(uint16_t)(((a>>30)&1U)|((b>>6)&2U));
    for(i=0;i<3;i++){
        if(states[i]==previous[i])continue;
        delta=pcd_quadrature_step(previous[i],states[i]);
        if(delta==2)errors[i]++;else counts[i]+=(int32_t)delta;
        previous[i]=states[i];
    }
}
#pragma CODE_SECTION(publish,".TI.ramfunc")
static void publish(void) {
    uint16_t index=shared->active_slot^1U,i;
    volatile Cpu2EncoderSnapshot *s=&shared->slot[index];
    sequence+=2;s->sequence_begin=sequence|1U;
    for(i=0;i<3;i++){s->motor_count[i]=(int32_t)counts[i];s->transition_error[i]=errors[i];}
    s->max_poll_cycles=gMaxPollCycles;s->sequence_end=sequence;s->sequence_begin=sequence;
    shared->active_slot=index;shared->abi_version=IPC_LAYOUT_VERSION;
}
#pragma CODE_SECTION(run,".TI.ramfunc")
static void run(void) {
    uint32_t now,last,published,elapsed,a,b;
    a=GPIO_readPortData(GPIO_PORT_A);b=GPIO_readPortData(GPIO_PORT_B);
    previous[0]=(a>>24)&3U;previous[1]=(a>>26)&3U;previous[2]=((a>>30)&1U)|((b>>6)&2U);
    last=published=CPUTimer_getTimerCount(CPUTIMER1_BASE);
    while(true){
        now=CPUTimer_getTimerCount(CPUTIMER1_BASE);elapsed=last-now;last=now;
        if(elapsed>gMaxPollCycles)gMaxPollCycles=elapsed;
        poll();gPolls++;
        if((uint32_t)(published-now)>=DEVICE_SYSCLK_FREQ/5000U){published=now;publish();}
    }
}
void main(void) {
    Device_init();
    Interrupt_initModule();
    memset((void *)shared,0,sizeof(*shared));
    while(*ready!=IPC_CPU2_START){}
    CPUTimer_stopTimer(CPUTIMER1_BASE);CPUTimer_setPreScaler(CPUTIMER1_BASE,0);
    CPUTimer_setPeriod(CPUTIMER1_BASE,0xffffffffUL);CPUTimer_reloadTimerCounter(CPUTIMER1_BASE);
    CPUTimer_startTimer(CPUTIMER1_BASE);
    run();
}
