//#############################################################################
//
// FILE:   encoders.c
//
// TITLE:  6-axis encoder driver for F28379D (CPU1)
//
// DESCRIPTION:
//   Axes 0-2: EQEP1, EQEP2, EQEP3 hardware quadrature decoders on CPU1.
//   Axes 3-5: GPIO quadrature on CPU2 — read from IPC shared RAM.
//
//   Adjust the GPIO pin defines below to match your PCB layout.
//   Enable USE_EQEPx_INDEX_RESET = 1 to zero the counter on index pulse.
//
//#############################################################################

#include "encoders.h"
#include <string.h>

//
// ============ Pin assignments — update to match your PCB ============
//
// EQEP1 (axis 0)
#define EQEP1_GPIO_A  20U
#define EQEP1_GPIO_B  21U
#define EQEP1_GPIO_I  23U

// EQEP2 (axis 1)
#define EQEP2_GPIO_A  54U
#define EQEP2_GPIO_B  55U
#define EQEP2_GPIO_I  57U

// EQEP3 (axis 2) — locked because it is already wired.
#define EQEP3_GPIO_A  62U
#define EQEP3_GPIO_B  63U
#define EQEP3_GPIO_I  65U

// Set to 1 to reset position counter on index pulse
#define USE_EQEP1_INDEX_RESET  0
#define USE_EQEP2_INDEX_RESET  0
#define USE_EQEP3_INDEX_RESET  0

// Reject sub-microsecond level-shifter glitches without limiting the present
// encoder rate. Three samples at 5 MHz require an input to stay stable ~0.6 us.
#define EQEP_QUAL_SAMPLE_HZ    5000000U

//
// EQEP status bits that can be cleared by writing back to QEPSTS
//
#define QEPSTS_CLEARABLE_MASK \
    (EQEP_QEPSTS_PCEF | EQEP_QEPSTS_FIMF | EQEP_QEPSTS_CDEF | \
     EQEP_QEPSTS_COEF | EQEP_QEPSTS_QDLF | EQEP_QEPSTS_FIDF | \
     EQEP_QEPSTS_UPEVNT)

//
// ============ IPC shared RAM ============
//
// CPU2TOCPU1RAM starts at 0x03F800 (CPU1 memory map).
// CPU2 writes Cpu2EncoderData_t at the base of this region.
// We access it as a pointer to the fixed hardware address.
// CPU1 is read-only here (hardware enforced for CPU2-owned data).
//
static volatile Cpu2EncoderData_t * const gCpu2Data =
    (volatile Cpu2EncoderData_t *)0x03F800UL;

//
// ============ Public state ============
//
volatile int32_t  gEncPos[6]    = {0};
volatile uint32_t gEncErrors[6] = {0};

//
// Offset applied when Encoders_zeroPosition() is called on CPU2 axes
//
static int32_t  s_cpu2_offset[3] = {0};

//
// Per-axis error accumulation helpers
//
static uint32_t s_phase_err[3]   = {0};
static uint32_t s_pos_err[3]     = {0};

//
// ============ Internal helpers ============
//
static void eqep_gpio_setup(uint32_t gpioA, uint32_t gpioB,
                             uint32_t pinCfgA, uint32_t pinCfgB,
                             bool useIndex, uint32_t gpioI, uint32_t pinCfgI)
{
    GPIO_setDirectionMode(gpioA, GPIO_DIR_MODE_IN);
    GPIO_setDirectionMode(gpioB, GPIO_DIR_MODE_IN);
    GPIO_setPadConfig(gpioA, GPIO_PIN_TYPE_STD);
    GPIO_setPadConfig(gpioB, GPIO_PIN_TYPE_STD);
    GPIO_setQualificationMode(gpioA, GPIO_QUAL_ASYNC);
    GPIO_setQualificationMode(gpioB, GPIO_QUAL_ASYNC);
    GPIO_setPinConfig(pinCfgA);
    GPIO_setPinConfig(pinCfgB);

    if (useIndex) {
        GPIO_setDirectionMode(gpioI, GPIO_DIR_MODE_IN);
        GPIO_setPadConfig(gpioI, GPIO_PIN_TYPE_STD);
        GPIO_setQualificationMode(gpioI, GPIO_QUAL_ASYNC);
        GPIO_setPinConfig(pinCfgI);
    }
}

static void eqep_module_setup(uint32_t base, uint32_t resetMode)
{
    EQEP_disableModule(base);
    EQEP_setDecoderConfig(base,
        (uint16_t)(EQEP_CONFIG_QUADRATURE |
                   EQEP_CONFIG_2X_RESOLUTION |
                   EQEP_CONFIG_NO_SWAP));
    EQEP_setPositionCounterConfig(base, (EQEP_PositionResetMode)resetMode,
                                  0xFFFFFFFFU);
    EQEP_setEmulationMode(base, EQEP_EMULATIONMODE_RUNFREE);
    EQEP_clearStatus(base, 0xFFFFU);
    EQEP_enableModule(base);
    EQEP_setInitialPosition(base, 0U);
    EQEP_setPosition(base, 0U);
}

static void eqep_log_errors(uint32_t base, uint32_t axisIdx)
{
    uint16_t flg = HWREGH(base + EQEP_O_QFLG);
    if (flg & EQEP_QFLG_PHE) { s_phase_err[axisIdx]++; gEncErrors[axisIdx]++; }
    if (flg) { HWREGH(base + EQEP_O_QCLR) = flg; }

    uint16_t sts = HWREGH(base + EQEP_O_QEPSTS);
    if (sts & EQEP_QEPSTS_PCEF) { s_pos_err[axisIdx]++; gEncErrors[axisIdx]++; }
    uint16_t clear_bits = sts & QEPSTS_CLEARABLE_MASK;
    if (clear_bits) { HWREGH(base + EQEP_O_QEPSTS) = clear_bits; }
}

static void apply_qual(uint32_t gpio, EncoderQualMode mode, uint32_t f_hz)
{
    if (mode == ENCODER_QUAL_ASYNC || f_hz == 0U) {
        GPIO_setQualificationMode(gpio, GPIO_QUAL_ASYNC);
        return;
    }
    GPIO_QualificationMode qm;
    switch (mode) {
    case ENCODER_QUAL_3SAMPLE: qm = GPIO_QUAL_3SAMPLE; break;
    case ENCODER_QUAL_6SAMPLE: qm = GPIO_QUAL_6SAMPLE; break;
    default:                   qm = GPIO_QUAL_6SAMPLE; break;
    }
    uint32_t n = (uint32_t)(DEVICE_SYSCLK_FREQ / (2U * f_hz));
    if (n > 255U) n = 255U;
    GPIO_setQualificationMode(gpio, qm);
    GPIO_setQualificationPeriod(gpio, (uint16_t)n);
}

//
// ============ Encoders_init ============
//
void Encoders_init(void)
{
    memset((void *)gEncPos,    0, sizeof(gEncPos));
    memset((void *)gEncErrors, 0, sizeof(gEncErrors));
    memset(s_cpu2_offset, 0, sizeof(s_cpu2_offset));
    memset(s_phase_err,   0, sizeof(s_phase_err));
    memset(s_pos_err,     0, sizeof(s_pos_err));

    // Enable peripheral clocks
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_EQEP1);
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_EQEP2);
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_EQEP3);

    // GPIO + mux for EQEP1
    eqep_gpio_setup(EQEP1_GPIO_A, EQEP1_GPIO_B,
                    GPIO_20_EQEP1A, GPIO_21_EQEP1B,
                    true, EQEP1_GPIO_I, GPIO_23_EQEP1I);

    // GPIO + mux for EQEP2
    eqep_gpio_setup(EQEP2_GPIO_A, EQEP2_GPIO_B,
                    GPIO_54_EQEP2A, GPIO_55_EQEP2B,
                    true, EQEP2_GPIO_I, GPIO_57_EQEP2I);

    // GPIO + mux for EQEP3
    eqep_gpio_setup(EQEP3_GPIO_A, EQEP3_GPIO_B,
                    GPIO_62_EQEP3A, GPIO_63_EQEP3B,
                    true, EQEP3_GPIO_I, GPIO_65_EQEP3I);

    apply_qual(EQEP1_GPIO_A, ENCODER_QUAL_3SAMPLE, EQEP_QUAL_SAMPLE_HZ);
    apply_qual(EQEP1_GPIO_B, ENCODER_QUAL_3SAMPLE, EQEP_QUAL_SAMPLE_HZ);
    apply_qual(EQEP2_GPIO_A, ENCODER_QUAL_3SAMPLE, EQEP_QUAL_SAMPLE_HZ);
    apply_qual(EQEP2_GPIO_B, ENCODER_QUAL_3SAMPLE, EQEP_QUAL_SAMPLE_HZ);
    apply_qual(EQEP3_GPIO_A, ENCODER_QUAL_3SAMPLE, EQEP_QUAL_SAMPLE_HZ);
    apply_qual(EQEP3_GPIO_B, ENCODER_QUAL_3SAMPLE, EQEP_QUAL_SAMPLE_HZ);

    // Module config
    eqep_module_setup(EQEP1_BASE,
                      USE_EQEP1_INDEX_RESET ? EQEP_POSITION_RESET_IDX
                                            : EQEP_POSITION_RESET_MAX_POS);
    eqep_module_setup(EQEP2_BASE,
                      USE_EQEP2_INDEX_RESET ? EQEP_POSITION_RESET_IDX
                                            : EQEP_POSITION_RESET_MAX_POS);
    eqep_module_setup(EQEP3_BASE,
                      USE_EQEP3_INDEX_RESET ? EQEP_POSITION_RESET_IDX
                                            : EQEP_POSITION_RESET_MAX_POS);

    // Initial read
    gEncPos[0] = (int32_t)EQEP_getPosition(EQEP1_BASE);
    gEncPos[1] = (int32_t)EQEP_getPosition(EQEP2_BASE);
    gEncPos[2] = (int32_t)EQEP_getPosition(EQEP3_BASE);
}

//
// ============ Encoders_updatePosition ============
//
void Encoders_updatePosition(void)
{
    // EQEP axes (0-2)
    gEncPos[0] = (int32_t)EQEP_getPosition(EQEP1_BASE);
    gEncPos[1] = (int32_t)EQEP_getPosition(EQEP2_BASE);
    gEncPos[2] = (int32_t)EQEP_getPosition(EQEP3_BASE);

    // Log EQEP error flags
    eqep_log_errors(EQEP1_BASE, 0U);
    eqep_log_errors(EQEP2_BASE, 1U);
    eqep_log_errors(EQEP3_BASE, 2U);

    // CPU2 axes (3-5) — read from IPC shared RAM (CPU2TOCPU1RAM @ 0x03F800)
    gEncPos[3] = gCpu2Data->enc_pos[0] - s_cpu2_offset[0];
    gEncPos[4] = gCpu2Data->enc_pos[1] - s_cpu2_offset[1];
    gEncPos[5] = gCpu2Data->enc_pos[2] - s_cpu2_offset[2];

    gEncErrors[3] = gCpu2Data->err_count[0];
    gEncErrors[4] = gCpu2Data->err_count[1];
    gEncErrors[5] = gCpu2Data->err_count[2];
}

//
// ============ Encoders_zeroPosition ============
//
void Encoders_zeroPosition(void)
{
    EQEP_setPosition(EQEP1_BASE, 0U);
    EQEP_setPosition(EQEP2_BASE, 0U);
    EQEP_setPosition(EQEP3_BASE, 0U);

    gEncPos[0] = 0;
    gEncPos[1] = 0;
    gEncPos[2] = 0;

    s_cpu2_offset[0] = gCpu2Data->enc_pos[0];
    s_cpu2_offset[1] = gCpu2Data->enc_pos[1];
    s_cpu2_offset[2] = gCpu2Data->enc_pos[2];
}

//
// ============ Qualification helpers ============
//
void Encoders_applyQualificationEQEP1(EncoderQualMode mode, uint32_t f_qual_hz,
                                       bool includeIndex)
{
    apply_qual(EQEP1_GPIO_A, mode, f_qual_hz);
    apply_qual(EQEP1_GPIO_B, mode, f_qual_hz);
    if (includeIndex) apply_qual(EQEP1_GPIO_I, mode, f_qual_hz);
}

void Encoders_applyQualificationEQEP2(EncoderQualMode mode, uint32_t f_qual_hz,
                                       bool includeIndex)
{
    apply_qual(EQEP2_GPIO_A, mode, f_qual_hz);
    apply_qual(EQEP2_GPIO_B, mode, f_qual_hz);
    if (includeIndex) apply_qual(EQEP2_GPIO_I, mode, f_qual_hz);
}

void Encoders_applyQualificationEQEP3(EncoderQualMode mode, uint32_t f_qual_hz,
                                       bool includeIndex)
{
    apply_qual(EQEP3_GPIO_A, mode, f_qual_hz);
    apply_qual(EQEP3_GPIO_B, mode, f_qual_hz);
    if (includeIndex) apply_qual(EQEP3_GPIO_I, mode, f_qual_hz);
}
