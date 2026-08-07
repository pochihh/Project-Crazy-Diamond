//#############################################################################
//
// FILE:   cla_setup.c
//
// TITLE:  CLA initialization — memory config, timer trigger, program copy
//
//#############################################################################

#include "cla_setup.h"
#include "cla_pid.h"
#include "driverlib.h"
#include "device.h"
#include <string.h>

//
// ============ Shared CLA variables (owned here, externs in cla_pid.h) ============
//
#pragma DATA_SECTION(gCpuToCla, "CpuToCla1MsgRAM")
volatile CpuToClaMsg_t gCpuToCla;

#pragma DATA_SECTION(gClaToCpu, "Cla1ToCpuMsgRAM")
volatile ClaToCpuMsg_t gClaToCpu;

#pragma DATA_SECTION(gClaGains, "Cla1DataRam0")
ClaGains_t gClaGains;

//
// Integrators and previous errors — defined + placed in cla_pid.cla.
// CPU externs them here for CLA_resetIntegrators() to zero them.
//
extern float gInteg[CLA_NUM_AXES];
extern float gErrPrev[CLA_NUM_AXES];

//
// CLA program copy symbols (provided by linker, EABI: no underscore)
//
extern uint16_t Cla1funcsLoadStart;
extern uint16_t Cla1funcsLoadSize;
extern uint16_t Cla1funcsRunStart;

//
// Optional EOT callback
//
static CLA_task1DoneCallback g_task1DoneCb = NULL;

//
// ============ CLA Task 1 EOT ISR ============
//
// Fires in PIE group 11 after CLA Task 1 completes.
// Calls the registered callback so main.c can drive motors immediately.
//
#pragma CODE_SECTION(CLA1_Task_1_ISR, ".TI.ramfunc")
__interrupt void CLA1_Task_1_ISR(void)
{
    if (g_task1DoneCb) {
        g_task1DoneCb();
    }
    CLA_clearTaskFlags(CLA1_BASE, CLA_TASKFLAG_1);
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP11);
}

//
// ============ CLA_setup ============
//
void CLA_setup(uint32_t rate_hz)
{
    uint16_t k;

    //
    // Zero all shared data before CLA runs
    //
    memset((void *)&gCpuToCla, 0, sizeof(gCpuToCla));
    memset((void *)&gClaToCpu, 0, sizeof(gClaToCpu));
    memset(&gClaGains, 0, sizeof(gClaGains));
    for (k = 0; k < CLA_NUM_AXES; k++) {
        gClaGains.u_max[k] = 1.0f;   // default: full range output
        gInteg[k]          = 0.0f;
        gErrPrev[k]        = 0.0f;
    }

    //
    // Configure LSRAM controller select:
    //   RAMLS4 → CLA program RAM (CPU+CLA1 access so debugger can write it)
    //   RAMLS5 → CLA data RAM    (CPU+CLA1 dual access for shared gains)
    //
    EALLOW;
    MemCfg_setLSRAMControllerSel(MEMCFG_SECT_LS4, MEMCFG_LSRAMCONTROLLER_CPU_CLA1);
    MemCfg_setLSRAMControllerSel(MEMCFG_SECT_LS5, MEMCFG_LSRAMCONTROLLER_CPU_CLA1);
    MemCfg_setCLAMemType(MEMCFG_SECT_LS4, MEMCFG_CLA_MEM_PROGRAM);
    MemCfg_setCLAMemType(MEMCFG_SECT_LS5, MEMCFG_CLA_MEM_DATA);
    EDIS;

    //
    // In a FLASH build, copy the CLA program from flash to RAMLS4.
    // In a RAM debug build, the debugger loads it directly — no copy needed.
    //
#ifdef _FLASH
    memcpy(&Cla1funcsRunStart, &Cla1funcsLoadStart,
           (size_t)(uint32_t)&Cla1funcsLoadSize);
#endif

    //
    // Initialize CLA message RAMs (clear ECC bits, etc.)
    //
    MemCfg_initSections(MEMCFG_SECT_MSGCPUTOCLA1 | MEMCFG_SECT_MSGCLA1TOCPU);
    while (MemCfg_getInitStatus(MEMCFG_SECT_MSGCPUTOCLA1 |
                                MEMCFG_SECT_MSGCLA1TOCPU) != 1U) {}

    //
    // Enable CLA clock
    //
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_CLA1);

    // Type-1 CLA vectors contain the full 16-bit task entry address.
#pragma diag_suppress=770
    CLA_mapTaskVector(CLA1_BASE, CLA_MVECT_1, (uint16_t)&Cla1Task1);
#pragma diag_warning=770

    //
    // Route CPU Timer 0 to CLA Task 1
    //
    EALLOW;
    CLA_setTriggerSource(CLA_TASK_1, CLA_TRIGGER_TINT0);
    // Tasks 2-8: software trigger only (unused)
    CLA_setTriggerSource(CLA_TASK_2, CLA_TRIGGER_SOFTWARE);
    CLA_setTriggerSource(CLA_TASK_3, CLA_TRIGGER_SOFTWARE);
    CLA_setTriggerSource(CLA_TASK_4, CLA_TRIGGER_SOFTWARE);
    CLA_setTriggerSource(CLA_TASK_5, CLA_TRIGGER_SOFTWARE);
    CLA_setTriggerSource(CLA_TASK_6, CLA_TRIGGER_SOFTWARE);
    CLA_setTriggerSource(CLA_TASK_7, CLA_TRIGGER_SOFTWARE);
    CLA_setTriggerSource(CLA_TASK_8, CLA_TRIGGER_SOFTWARE);
    EDIS;

    //
    // Only Task 1 is used.
    //
    CLA_enableTasks(CLA1_BASE, CLA_TASKFLAG_1);

    //
    // Register and enable CLA Task 1 EOT interrupt (PIE 11.1).
    // The EOT interrupt fires automatically after each CLA Task 1 completion.
    // No CLA-side enable needed — just register and enable the CPU interrupt.
    //
    Interrupt_register(INT_CLA1_1, &CLA1_Task_1_ISR);
    Interrupt_enable(INT_CLA1_1);

    //
    // Configure CPU Timer 0 to generate TINT0 at the desired rate.
    //   Period = DEVICE_SYSCLK_FREQ / rate_hz  ticks (no prescaler)
    //
    if (rate_hz == 0U) rate_hz = 1000U;

    CPUTimer_stopTimer(CPUTIMER0_BASE);
    CPUTimer_setPreScaler(CPUTIMER0_BASE, 0U);
    CPUTimer_setPeriod(CPUTIMER0_BASE,
                       (uint32_t)(DEVICE_SYSCLK_FREQ / rate_hz) - 1U);
    CPUTimer_reloadTimerCounter(CPUTIMER0_BASE);
    CPUTimer_enableInterrupt(CPUTIMER0_BASE);
    CPUTimer_startTimer(CPUTIMER0_BASE);
}

//
// ============ CLA_setTask1DoneCallback ============
//
void CLA_setTask1DoneCallback(CLA_task1DoneCallback fn)
{
    g_task1DoneCb = fn;
}

//
// ============ CLA_setGains ============
//
void CLA_setGains(uint16_t axis, float kp, float ki, float kd, float u_max,
                  float control_rate_hz)
{
    if (axis >= CLA_NUM_AXES) return;
    if (control_rate_hz <= 0.0f) return;

    float dt = 1.0f / control_rate_hz;

    gClaGains.kp[axis]        = kp;
    gClaGains.ki_dt[axis]     = ki * dt;
    gClaGains.kd_inv_dt[axis] = kd / dt;
    gClaGains.u_max[axis]     = (u_max > 0.0f) ? u_max : 1.0f;
}

//
// ============ CLA_resetIntegrators ============
//
void CLA_resetIntegrators(uint16_t axis)
{
    uint16_t k;
    if (axis == 0xFFU) {
        for (k = 0; k < CLA_NUM_AXES; k++) {
            gInteg[k]   = 0.0f;
            gErrPrev[k] = 0.0f;
        }
    } else if (axis < CLA_NUM_AXES) {
        gInteg[axis]   = 0.0f;
        gErrPrev[axis] = 0.0f;
    }
}
