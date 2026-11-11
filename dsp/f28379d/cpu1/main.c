/* PCD hardware endpoint: 5 kHz latest-value snapshot, host-owned controllers. */
#include "driverlib.h"
#include "device.h"
#include "spi_dma.h"
#include "encoders.h"
#include "motor.h"
#include "io.h"
#include "ipc_layout.h"
#include "../../common/endpoint.h"

static PcdEndpoint endpoint;
volatile uint32_t gTickCount,gMaxTickUs;
static uint16_t applied_mask;
#pragma CODE_SECTION(micros,".TI.ramfunc")
static uint32_t micros(void){return 0xffffffffUL-CPUTimer_getTimerCount(CPUTIMER1_BASE);}
#pragma CODE_SECTION(onSpiFrame,".TI.ramfunc")
static void onSpiFrame(const RxFrame_t *c){pcd_accept(&endpoint,c,micros(),gEncoderValid);}
#pragma CODE_SECTION(tick,".TI.ramfunc")
__interrupt void tick(void){
    PcdTelemetry t={0};uint16_t i;uint32_t started=micros(),duration;
    Encoders_updatePosition(started);pcd_tick(&endpoint,started,gEncoderValid);
    if(applied_mask!=endpoint.mask){
        Motor_stop();if(endpoint.mask)Motor_arm(endpoint.mask);applied_mask=endpoint.mask;
    }
    for(i=0;i<PCD_AXES;i++){
        t.requested_duty[i]=endpoint.command.duty[i];
        t.duty[i]=(endpoint.mask&(1U<<i))?endpoint.command.duty[i]:0;
        t.reference[i]=endpoint.command.reference[i];t.position[i]=gEncPos[i];t.encoder_errors+=gEncErrors[i];
    }
    Motor_setAllOutputs(t.duty);
    Io_tick(&t,&endpoint.command);
    t.timestamp_us=started;t.sequence=++gTickCount;t.session=endpoint.command.session;
    t.command_sequence=endpoint.command.sequence;t.state=endpoint.fault?2:(endpoint.active?1:0);
    t.arm_mask=endpoint.mask;t.fault=endpoint.fault;t.encoder_valid=gEncoderValid;
    t.cpu2_max_poll_us=gCpu2MaxPollUs;
    t.invalid_frames=SpiDma_getStatsPtr()->crc_errors+SpiDma_getStatsPtr()->frame_errors;
    t.rejected_commands=endpoint.rejected;t.watchdogs=endpoint.watchdogs;
    SpiDma_updateTx(&t);
    duration=micros()-started;if(duration>gMaxTickUs)gMaxTickUs=duration;
    CPUTimer_clearOverflowFlag(CPUTIMER0_BASE);
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}
static void cpu2_pins(void){
    const uint32_t pins[6]={24,25,26,27,30,39};uint16_t i;
    GPIO_setPinConfig(GPIO_24_GPIO24);GPIO_setPinConfig(GPIO_25_GPIO25);
    GPIO_setPinConfig(GPIO_26_GPIO26);GPIO_setPinConfig(GPIO_27_GPIO27);
    GPIO_setPinConfig(GPIO_30_GPIO30);GPIO_setPinConfig(GPIO_39_GPIO39);
    for(i=0;i<6;i++){
        GPIO_setDirectionMode(pins[i],GPIO_DIR_MODE_IN);
        GPIO_setPadConfig(pins[i],GPIO_PIN_TYPE_STD);
        GPIO_setQualificationPeriod(pins[i],20);
        GPIO_setQualificationMode(pins[i],GPIO_QUAL_3SAMPLE);
        GPIO_setMasterCore(pins[i],GPIO_CORE_CPU2);
    }
    *(volatile uint16_t *)0x03fc00UL=IPC_CPU2_START;
}
void main(void){
#ifdef _FLASH
    bool cpu2_boot_requested=false;
#endif
    Device_init();Device_initGPIO();Interrupt_initModule();Interrupt_initVectorTable();
    Motor_init();Motor_stop();
    CPUTimer_stopTimer(CPUTIMER1_BASE);
    CPUTimer_setPreScaler(CPUTIMER1_BASE,(uint16_t)(DEVICE_SYSCLK_FREQ/1000000UL)-1U);
    CPUTimer_setPeriod(CPUTIMER1_BASE,0xffffffffUL);CPUTimer_reloadTimerCounter(CPUTIMER1_BASE);
    CPUTimer_startTimer(CPUTIMER1_BASE);
    Encoders_init();cpu2_pins();Io_init();
    SpiDma_init();SpiDma_setRxCallback(onSpiFrame);
    CPUTimer_stopTimer(CPUTIMER0_BASE);CPUTimer_setPreScaler(CPUTIMER0_BASE,0);
    CPUTimer_setPeriod(CPUTIMER0_BASE,DEVICE_SYSCLK_FREQ/5000UL-1U);
    CPUTimer_reloadTimerCounter(CPUTIMER0_BASE);CPUTimer_clearOverflowFlag(CPUTIMER0_BASE);
    Interrupt_register(INT_TIMER0,tick);Interrupt_enable(INT_TIMER0);
    CPUTimer_enableInterrupt(CPUTIMER0_BASE);CPUTimer_startTimer(CPUTIMER0_BASE);
    EINT;ERTM;
    while(true){
#ifdef _FLASH
        // ROM readiness is asynchronous; retry without blocking CPU1's I/O tick.
        if(!cpu2_boot_requested && gEncoderValid!=63U &&
           (HWREG(IPC_BASE+IPC_O_BOOTSTS)&C2_BOOTROM_BOOTSTS_SYSTEM_READY)!=0 &&
           (HWREG(IPC_BASE+IPC_O_FLG)&(IPC_FLG_IPC0|IPC_FLG_IPC31))==0){
            Device_bootCPU2(C1C2_BROM_BOOTMODE_BOOT_FROM_FLASH);
            cpu2_boot_requested=true;
        }
#endif
    }
}
