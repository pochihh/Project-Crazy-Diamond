#include "io.h"
#include "driverlib.h"
#include "device.h"
static const uint32_t inputs[4]={32,33,80,81};
static const uint32_t outputs[4]={35,36,37,38};
static uint16_t samples[PCD_AI],valid;
void Io_init(void) {
    uint16_t i;
    GPIO_setPinConfig(GPIO_32_GPIO32);GPIO_setPinConfig(GPIO_33_GPIO33);
    GPIO_setPinConfig(GPIO_80_GPIO80);GPIO_setPinConfig(GPIO_81_GPIO81);
    GPIO_setPinConfig(GPIO_35_GPIO35);GPIO_setPinConfig(GPIO_36_GPIO36);
    GPIO_setPinConfig(GPIO_37_GPIO37);GPIO_setPinConfig(GPIO_38_GPIO38);
    for(i=0;i<4;i++){
        GPIO_setDirectionMode(inputs[i],GPIO_DIR_MODE_IN);
        GPIO_setPadConfig(inputs[i],GPIO_PIN_TYPE_PULLUP);
        GPIO_setQualificationMode(inputs[i],GPIO_QUAL_3SAMPLE);
        GPIO_writePin(outputs[i],0);GPIO_setDirectionMode(outputs[i],GPIO_DIR_MODE_OUT);
        GPIO_setPadConfig(outputs[i],GPIO_PIN_TYPE_STD);
    }
    ADC_setPrescaler(ADCB_BASE,ADC_CLK_DIV_4_0);
    ADC_setMode(ADCB_BASE,ADC_RESOLUTION_12BIT,ADC_MODE_SINGLE_ENDED);
    ADC_setInterruptPulseMode(ADCB_BASE,ADC_PULSE_END_OF_CONV);
    for(i=0;i<PCD_AI;i++) ADC_setupSOC(ADCB_BASE,(ADC_SOCNumber)i,ADC_TRIGGER_SW_ONLY,(ADC_Channel)i,64);
    ADC_setInterruptSource(ADCB_BASE,ADC_INT_NUMBER1,ADC_SOC_NUMBER5);
    ADC_enableInterrupt(ADCB_BASE,ADC_INT_NUMBER1);
    ADC_clearInterruptStatus(ADCB_BASE,ADC_INT_NUMBER1);
    ADC_enableConverter(ADCB_BASE);
    /* DAC A/B use ADCA VREFHI; pins A0/A1 are outputs, never ADC inputs here. */
    DAC_setReferenceVoltage(DACA_BASE,DAC_REF_ADC_VREFHI);
    DAC_setReferenceVoltage(DACB_BASE,DAC_REF_ADC_VREFHI);
    DAC_setShadowValue(DACA_BASE,0);DAC_setShadowValue(DACB_BASE,0);
    DAC_enableOutput(DACA_BASE);DAC_enableOutput(DACB_BASE);
    DEVICE_DELAY_US(1000);
    ADC_forceMultipleSOC(ADCB_BASE,0x003f);
}
#pragma CODE_SECTION(Io_tick,".TI.ramfunc")
void Io_tick(PcdTelemetry *t,const PcdCommand *c) {
    uint16_t i;
    t->digital_in=0;t->digital_out=c->digital_out;
    for(i=0;i<4;i++){
        GPIO_writePin(outputs[i],(c->digital_out>>i)&1U);
        t->digital_in|=(uint16_t)(GPIO_readPin(inputs[i])<<i);
    }
    if(ADC_getInterruptStatus(ADCB_BASE,ADC_INT_NUMBER1)){
        for(i=0;i<PCD_AI;i++)samples[i]=ADC_readResult(ADCBRESULT_BASE,(ADC_SOCNumber)i);
        valid=0x3f;
        ADC_clearInterruptStatus(ADCB_BASE,ADC_INT_NUMBER1);
        ADC_clearInterruptOverflowStatus(ADCB_BASE,ADC_INT_NUMBER1);
    } else valid=0; /* Do not advertise old ADC data as a fresh acquisition. */
    for(i=0;i<PCD_AI;i++)t->analog_in[i]=samples[i];
    t->analog_valid=valid;t->analog_out_valid=3;
    for(i=0;i<PCD_AO;i++)t->analog_out[i]=c->analog_out[i];
    DAC_setShadowValue(DACA_BASE,c->analog_out[0]);DAC_setShadowValue(DACB_BASE,c->analog_out[1]);
    ADC_forceMultipleSOC(ADCB_BASE,0x003f);
}
