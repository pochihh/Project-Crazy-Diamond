#include "board.h"
#include "c2000ware_libraries.h"
#include "device.h"
#include "driverlib.h"
#include "encoders.h"
#include "MessageCenter.h"
#include "motor.h"
#include "util.h"

#define FIRMWARE_VERSION "0.2.1" // added cmd timeout safety feature

// ==== CLA shared data ====
#pragma DATA_SECTION(gCpuToCla, "CpuToCla1MsgRAM");
volatile CpuToClaMsg gCpuToCla;

#pragma DATA_SECTION(gClaToCpu, "Cla1ToCpuMsgRAM");
volatile ClaToCpuMsg gClaToCpu;

RxFrame_t cmd_rev;
TxFrame_t data_sent;
statusLED led_blue, led_red;

// for ADC data
uint16_t gAnalogIn[4]; // analog input readings

// Enable GPIO input qualification on eQEP pins (e.g. for slow/noisy encoders).
// WARNING: qualification is in the peripheral input path on F2837xD — enabling
// it will cause the position counter to freeze once the encoder edge rate
// exceeds ~65 kHz (roughly 1500-2500 RPM depending on CPR).
// Can also be set via a project-level compiler define.
#ifndef USE_EQEP_QUALIFICATION
#define USE_EQEP_QUALIFICATION 0
#endif

#define CMD_SET_REFERENCE 0x1005
#define CMD_RESET_ENCODER 0x2000
#define CMD_RESET_ENCODER_ERROR 0x2001

static uint32_t gLastCommandTimeUs = 0;
#define CMD_TIMEOUT_US 50000 // 50ms

static void onCommand(const volatile uint16_t *cmd) {
  // log the command time
  gLastCommandTimeUs = micros();
  
  // Handle cmd/ref1/ref2 as needed (set references, etc.)
  cmd_rev.cmd = ((uint32_t)cmd[1] << 16) | cmd[0];

  switch (cmd_rev.cmd) {
    case CMD_SET_REFERENCE:
      // unpack the reference values
      cmd_rev.ref1 = unpack_float(cmd[2], cmd[3]);
      cmd_rev.ref2 = unpack_float(cmd[4], cmd[5]);

      // update the CPU to CLA message
      gCpuToCla.ref_1 = cmd_rev.ref1;
      gCpuToCla.ref_2 = cmd_rev.ref2;
      break;
    default:
      break;
  }
}

static void prepareDataToSend(TxFrame_t *out) {
  out->timestamp_us = micros(); // timestamp in us
  // out->timestamp_us = 777; // timestamp in us
  out->pos_1 = gClaToCpu.pos_1;
  out->pos_2 = gClaToCpu.pos_2;
  out->err_1 = encoder1_error_count;
  out->err_2 = encoder2_error_count;
  out->ref_1 = gClaToCpu.ref_1;
  out->ref_2 = gClaToCpu.ref_2;
  out->u_1 = gClaToCpu.u_1;
  out->u_2 = gClaToCpu.u_2;
  out->analogIn[0] = gAnalogIn[0];
  out->analogIn[1] = gAnalogIn[1];
  out->analogIn[2] = gAnalogIn[2];
  out->analogIn[3] = gAnalogIn[3];
}

__interrupt void CLA1_Task_1_ISR(void) {
  // Perform H-bridge control
  // motorControl(gClaToCpu.u_1, gClaToCpu.u_2);
  motorControl(gClaToCpu.u_1, gClaToCpu.u_2);

  // prepare data and send; include ADC data copy (disabled for DMA test)
  prepareDataToSend(&data_sent);
  MessageCenter_tick(&data_sent);

  // Clear the CLA interrupt flag
  CLA_clearTaskFlags(CLA1_BASE, CLA_TASKFLAG_1);
  // Acknowledge PIE group 11 so another CLA interrupt can occur
  Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP11);
}

void main(void) {
  // System / framework
  Device_init();
  Device_initGPIO();
  Interrupt_initModule();
  Interrupt_initVectorTable();
  Board_init(); // If SysConfig configures other pins, we override next
  C2000Ware_libraries_init();

  // Encoders (EQEP1: GPIO20/21[/23], EQEP2: GPIO54/55[/57])
  Encoders_init();
#if USE_EQEP_QUALIFICATION
  Encoders_applyQualificationEQEP1(ENCODER_QUAL_6SAMPLE, 2000, false);
  Encoders_applyQualificationEQEP2(ENCODER_QUAL_6SAMPLE, 2000, false);
#endif

  // Initialize encoder error counting
  encoder1_error_count = 0;
  encoder2_error_count = 0;

  // SPI setup
  MessageCenter_init();
  MessageCenter_setRxCallback(&onCommand);

  // init timer
  initMicrosecondTimer();

  // init leds
  statusLED_init(&led_blue, DEVICE_GPIO_PIN_LED1);
  statusLED_init(&led_red, DEVICE_GPIO_PIN_LED2);
  // disableStatusLED(&led_red); // start with red off

  // init ADC and H-bridges
  ADC_init();
  HBridge_init();

  // CLA bring-up (run Task1 at 1 kHz via CPU Timer0)
  CLA_bringUp_1kHz();

  // Prime initial references
  gCpuToCla.ref_1 = 0.f;
  gCpuToCla.ref_2 = 0.f;

  EINT; // Enable global interrupt (not required for this polled demo)
  ERTM;

  while (true) {

    // update encoder values
    Encoders_updatePosition();

    gCpuToCla.pos_1 = encoder1_position;
    gCpuToCla.pos_2 = encoder2_position;

    // update ADC values
    ADC_sampleTick();
    //ADC_sampleTick_C2_C3_D2_D3();
    
    // // prepare data and send (disabled for DMA test)
    // prepareDataToSend(&data_sent);
    // MessageCenter_tick(&data_sent);

    // update breathing LED
    statusLED_blink(&led_blue);

    // cla heartbeat
    static uint64_t hb_cnt = 0U;
    if (gClaToCpu.cycleCount - hb_cnt > 1000) {
      hb_cnt = gClaToCpu.cycleCount;
      statusLED_toggle(&led_red); // proven working in your CPU code
    }

    // check if the command has timed out
    if (micros() - gLastCommandTimeUs > CMD_TIMEOUT_US) {
      // reset the command
      gCpuToCla.ref_1 = 0.f;
      gCpuToCla.ref_2 = 0.f;
    }
  }
}
