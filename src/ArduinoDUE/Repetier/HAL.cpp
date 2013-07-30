/*
    This file is part of Repetier-Firmware.

    Repetier-Firmware is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Repetier-Firmware is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Repetier-Firmware.  If not, see <http://www.gnu.org/licenses/>.

    This firmware is a nearly complete rewrite of the sprinter firmware
    by kliment (https://github.com/kliment/Sprinter)
    which based on Tonokip RepRap firmware rewrite based off of Hydra-mmm firmware.

  
    
    Main author: repetier
 
    Initial port of hardware abstraction layer to Arduino Due: John Silvia
*/

#include "Repetier.h"
#include <malloc.h>

//extern "C" void __cxa_pure_virtual() { }
extern "C" char *sbrk(int i);
extern long bresenham_step();

volatile byte insideTimer1=0;


HAL::HAL()
{
    //ctor
}

HAL::~HAL()
{
    //dtor
}


long HAL::CPUDivU2(unsigned int divisor)
{
    return F_CPU/divisor;
}

// Set up all timer interrupts 
void HAL::setupTimer() {
    uint32_t     tc_count, tc_clock;

    pmc_set_writeprotect(false);

    // set 3 bits for interrupt group priority, 2 bits for sub-priority
    NVIC_SetPriorityGrouping(4);

#if defined(USE_ADVANCE)
    // Timer for extruder control
    pmc_enable_periph_clk(EXTRUDER_TIMER_IRQ);  // enable power to timer
    NVIC_SetPriority((IRQn_Type)EXTRUDER_TIMER_IRQ, NVIC_EncodePriority(4, 1, 1));

    // count up to value in RC register using given clock
    TC_Configure(EXTRUDER_TIMER, EXTRUDER_TIMER_CHANNEL, TC_CMR_WAVSEL_UP_RC | TC_CMR_WAVE | TC_CMR_TCCLKS_TIMER_CLOCK4);

    TC_SetRC(EXTRUDER_TIMER, EXTRUDER_TIMER_CHANNEL, (F_CPU_TRUE / TIMER0_PRESCALE) / EXTRUDER_CLOCK_FREQ); // set frequency
    TC_Start(EXTRUDER_TIMER, EXTRUDER_TIMER_CHANNEL);           // start timer running
    
    // enable RC compare interrupt
    EXTRUDER_TIMER->TC_CHANNEL[EXTRUDER_TIMER_CHANNEL].TC_IER = TC_IER_CPCS;
    // clear the "disable RC compare" interrupt
    EXTRUDER_TIMER->TC_CHANNEL[EXTRUDER_TIMER_CHANNEL].TC_IDR = ~TC_IER_CPCS;

    // allow interrupts on timer
    NVIC_EnableIRQ((IRQn_Type)EXTRUDER_TIMER_IRQ);
#endif
    // Regular interrupts for heater control etc
    pmc_enable_periph_clk(PWM_TIMER_IRQ);
    NVIC_SetPriority((IRQn_Type)PWM_TIMER_IRQ, NVIC_EncodePriority(4, 3, 0));
   
    TC_FindMckDivisor(PWM_CLOCK_FREQ, F_CPU_TRUE, &tc_count, &tc_clock, F_CPU_TRUE);  
    TC_Configure(PWM_TIMER, PWM_TIMER_CHANNEL, TC_CMR_WAVSEL_UP_RC | TC_CMR_WAVE | tc_clock);

    TC_SetRC(PWM_TIMER, PWM_TIMER_CHANNEL, (F_CPU_TRUE / tc_count) / PWM_CLOCK_FREQ);
    TC_Start(PWM_TIMER, PWM_TIMER_CHANNEL);
 
    PWM_TIMER->TC_CHANNEL[PWM_TIMER_CHANNEL].TC_IER = TC_IER_CPCS;
    PWM_TIMER->TC_CHANNEL[PWM_TIMER_CHANNEL].TC_IDR = ~TC_IER_CPCS;
    NVIC_EnableIRQ((IRQn_Type)PWM_TIMER_IRQ);

    // Timer for stepper motor control
    pmc_enable_periph_clk(TIMER1_TIMER_IRQ );
    NVIC_SetPriority((IRQn_Type)TIMER1_TIMER_IRQ, NVIC_EncodePriority(4, 1, 0));
      
    TC_Configure(TIMER1_TIMER, TIMER1_TIMER_CHANNEL, TC_CMR_WAVSEL_UP_RC | 
                 TC_CMR_WAVE | TC_CMR_TCCLKS_TIMER_CLOCK1);

    TC_SetRC(TIMER1_TIMER, TIMER1_TIMER_CHANNEL, (F_CPU_TRUE / TIMER1_PRESCALE) / TIMER1_CLOCK_FREQ);
    TC_Start(TIMER1_TIMER, TIMER1_TIMER_CHANNEL);

    TIMER1_TIMER->TC_CHANNEL[TIMER1_TIMER_CHANNEL].TC_IER = TC_IER_CPCS;
    TIMER1_TIMER->TC_CHANNEL[TIMER1_TIMER_CHANNEL].TC_IDR = ~TC_IER_CPCS;
    NVIC_EnableIRQ((IRQn_Type)TIMER1_TIMER_IRQ); 

    // Servo control
#if FEATURE_SERVO
#if SERVO0_PIN>-1
    SET_OUTPUT(SERVO0_PIN);
    WRITE(SERVO0_PIN,LOW);
#endif
#if SERVO1_PIN>-1
    SET_OUTPUT(SERVO1_PIN);
    WRITE(SERVO1_PIN,LOW);
#endif
#if SERVO2_PIN>-1
    SET_OUTPUT(SERVO2_PIN);
    WRITE(SERVO2_PIN,LOW);
#endif
#if SERVO3_PIN>-1
    SET_OUTPUT(SERVO3_PIN);
    WRITE(SERVO3_PIN,LOW);
#endif
    pmc_enable_periph_clk(SERVO_TIMER_IRQ );
    NVIC_SetPriority((IRQn_Type)SERVO_TIMER_IRQ, NVIC_EncodePriority(4, 2, 0));
      
    TC_FindMckDivisor(SERVO_CLOCK_FREQ, F_CPU_TRUE, &tc_count, &tc_clock, F_CPU_TRUE);
    TC_Configure(SERVO_TIMER, SERVO_TIMER_CHANNEL, TC_CMR_WAVSEL_UP_RC | 
                 TC_CMR_WAVE | tc_clock);

    TC_SetRC(SERVO_TIMER, SERVO_TIMER_CHANNEL, (F_CPU_TRUE / tc_count) / SERVO_CLOCK_FREQ);

    SERVO_TIMER->TC_CHANNEL[SERVO_TIMER_CHANNEL].TC_IER = TC_IER_CPCS;
    SERvo_TIMER->TC_CHANNEL[SERVO_TIMER_CHANNEL].TC_IDR = ~TC_IER_CPCS;
    NVIC_EnableIRQ((IRQn_Type)SERVO_TIMER_IRQ);
#endif
}



#if ANALOG_INPUTS>0
// Initialize ADC channels
void HAL::analogStart(void)
{
  uint32_t  adcEnable = 0;

  // ensure we can write to ADC registers
  ADC->ADC_WPMR = ADC_WPMR_WPKEY(0);
  pmc_enable_periph_clk(ID_ADC);  // enable adc clock

  for(int i=0; i<ANALOG_INPUTS; i++)
  {
      osAnalogInputCounter[i] = 0;
      osAnalogInputValues[i] = 0;
  // osAnalogInputChannels
      //adcEnable |= (0x1u << adcChannel[i]);
      adcEnable |= (0x1u << osAnalogInputChannels[i]);
  }

  // enable channels
  ADC->ADC_CHER = adcEnable;
  ADC->ADC_CHDR = !adcEnable;

  // Initialize ADC mode register (some of the following params are not used here)
  // HW trigger disabled, use external Trigger, 12 bit resolution
  // core and ref voltage stays on, normal sleep mode, normal not free-run mode
  // startup time 16 clocks, settling time 17 clocks, no changes on channel switch
  // convert channels in numeric order
  // set prescaler rate  MCK/((PRESCALE+1) * 2)
  // set tracking time  (TRACKTIM+1) * clock periods
  // set transfer period  (TRANSFER * 2 + 3) 
  ADC->ADC_MR = ADC_MR_TRGEN_DIS | ADC_MR_TRGSEL_ADC_TRIG0 | ADC_MR_LOWRES_BITS_10 |
            ADC_MR_SLEEP_NORMAL | ADC_MR_FWUP_OFF | ADC_MR_FREERUN_OFF |
            ADC_MR_STARTUP_SUT64 | ADC_MR_SETTLING_AST17 | ADC_MR_ANACH_NONE |
            ADC_MR_USEQ_NUM_ORDER |
            ADC_MR_PRESCAL(AD_PRESCALE_FACTOR) |
            ADC_MR_TRACKTIM(AD_TRACKING_CYCLES) |
            ADC_MR_TRANSFER(AD_TRANSFER_CYCLES);

  ADC->ADC_IER = 0;             // no ADC interrupts
  ADC->ADC_CGR = 0;             // Gain = 1
  ADC->ADC_COR = 0;             // Single-ended, no offset
  
  // start first conversion
  ADC->ADC_CR = ADC_CR_START;
}

#endif

// Print apparent cause of start/restart
void HAL::showStartReason() {
    int mcu = (RSTC->RSTC_SR & RSTC_SR_RSTTYP_Msk) >> RSTC_SR_RSTTYP_Pos;
    switch (mcu){
    case 0:
        Com::printInfoFLN(Com::tPowerUp);
        break;
    case 1:
        // this is return from backup mode on SAM
        Com::printInfoFLN(Com::tBrownOut);
    case 2:
        Com::printInfoFLN(Com::tWatchdog);
        break;
    case 3:
        Com::printInfoFLN(Com::tSoftwareReset);
        break;
    case 4:
        Com::printInfoFLN(Com::tExternalReset);
    } 
}

// Return available memory
int HAL::getFreeRam() {
    struct mallinfo memstruct = mallinfo();
    register char * stack_ptr asm ("sp");

    // avail mem in heap + (bottom of stack addr - end of heap addr)
    return (memstruct.fordblks + (int)stack_ptr -  (int)sbrk(0));
}

// Reset peripherals and cpu
void HAL::resetHardware() {
    RSTC->RSTC_CR = RSTC_CR_KEY(0xA5) | RSTC_CR_PERRST | RSTC_CR_PROCRST;
}


/*************************************************************************
 Initialization of the I2C bus interface. Need to be called only once
*************************************************************************/
void HAL::i2cInit(unsigned long clockSpeedHz)
{
    // enable TWI
	pmc_enable_periph_clk(TWI_ID);

    // Configure pins
	PIO_Configure(g_APinDescription[SDA_PIN].pPort,
                  g_APinDescription[SDA_PIN].ulPinType,
                  g_APinDescription[SDA_PIN].ulPin,
                  g_APinDescription[SDA_PIN].ulPinConfiguration);
	PIO_Configure(g_APinDescription[SCL_PIN].pPort,
                  g_APinDescription[SCL_PIN].ulPinType,
                  g_APinDescription[SCL_PIN].ulPin,
                  g_APinDescription[SCL_PIN].ulPinConfiguration);

    // Set to Master mode with known state
    TWI_INTERFACE->TWI_CR = TWI_CR_SVEN;
    TWI_INTERFACE->TWI_CR = TWI_CR_SWRST;
    TWI_INTERFACE->TWI_RHR;
    TWI_INTERFACE->TWI_IMR = 0;

    TWI_INTERFACE->TWI_CR = TWI_CR_SVDIS;
    TWI_INTERFACE->TWI_CR = TWI_CR_MSDIS;
    TWI_INTERFACE->TWI_CR = TWI_CR_MSEN; 

    // Set i2c clock rate
    uint32_t dwCkDiv = 0;
    uint32_t dwClDiv;
    while ( dwClDiv == 0 )
    {
        dwClDiv = ((F_CPU_TRUE / (2 * clockSpeedHz)) - 4) / (1<<dwCkDiv);

        if ( dwClDiv > 255 )
        {
            dwCkDiv++;
            dwClDiv = 0;
        }
    }
    TWI_INTERFACE->TWI_CWGR = 0;
    TWI_INTERFACE->TWI_CWGR = (dwCkDiv << 16) | (dwClDiv << 8) | dwClDiv;
}

/*************************************************************************
  Issues a start condition and sends address and transfer direction.
  return 0 = device accessible, 1= failed to access device
*************************************************************************/
unsigned char HAL::i2cStart(unsigned char address_and_direction)
{
    uint32_t twiDirection = address_and_direction & 1;
    uint32_t address = address_and_direction >> 1;

    TWI_INTERFACE->TWI_CR = TWI_CR_MSEN | TWI_CR_SVDIS;

    // set master mode register with no internal address
    TWI_INTERFACE->TWI_MMR = 0;
    TWI_INTERFACE->TWI_MMR = (twiDirection << 12) | TWI_MMR_IADRSZ_NONE |  
        TWI_MMR_DADR(address);

    // returning readiness to send/recieve not device accessibility
    // return value not used in code anyway
    return !(TWI_INTERFACE->TWI_SR & TWI_SR_TXCOMP);
}


/*************************************************************************
 Issues a start condition and sends address and transfer direction.
 If device is busy, use ack polling to wait until device is ready

 Input:   address and transfer direction of I2C device
*************************************************************************/
void HAL::i2cStartWait(unsigned char address_and_direction)
{
    uint32_t twiDirection = address_and_direction & 1;
    uint32_t address = address_and_direction >> 1;

    while(!(TWI_INTERFACE->TWI_SR & TWI_SR_TXCOMP));

    // set to master mode
    TWI_INTERFACE->TWI_CR = TWI_CR_MSEN | TWI_CR_SVDIS;

    // set master mode register with no internal address
    TWI_INTERFACE->TWI_MMR = 0;
    TWI_INTERFACE->TWI_MMR = (twiDirection << 12) | TWI_MMR_IADRSZ_NONE |  
         TWI_MMR_DADR(address);
}

/*************************************************************************
 Issues a start condition and sends address and transfer direction.
 Also specifies internal address of device

 Input:   address and transfer direction of I2C device, internal address
*************************************************************************/
void HAL::i2cStartAddr(unsigned char address_and_direction, unsigned int pos)
{
    uint32_t twiDirection = address_and_direction & 1;
    uint32_t address = address_and_direction >> 1;
    
    // if 1 byte address, eeprom uses lower address bits for pos > 255    
    if (EEPROM_ADDRSZ_BYTES == TWI_MMR_IADRSZ_1_BYTE)
    {
      address |= pos >> 8;
      pos &= 0xFF;
    }

    // set to master mode
    TWI_INTERFACE->TWI_CR = TWI_CR_MSEN | TWI_CR_SVDIS;

    // set master mode register with internal address
    TWI_INTERFACE->TWI_MMR = 0;
    TWI_INTERFACE->TWI_MMR = (twiDirection << 12) | EEPROM_ADDRSZ_BYTES |
         TWI_MMR_DADR(address);

    // write internal address register
    TWI_INTERFACE->TWI_IADR = TWI_IADR_IADR(pos);
}

/*************************************************************************
 Terminates the data transfer and releases the I2C bus
*************************************************************************/
void HAL::i2cStop(void)
{
    TWI_INTERFACE->TWI_CR = TWI_CR_STOP;
    i2cTxFinished();
    i2cCompleted ();
}

/*************************************************************************
 Signal start of data transfer
*************************************************************************/
void HAL::i2cStartBit(void)
{
    TWI_INTERFACE->TWI_CR = TWI_CR_START; 
}

/*************************************************************************
 Wait for transaction to complete
*************************************************************************/
void HAL::i2cCompleted (void)
{
    while(!((TWI_INTERFACE->TWI_SR & TWI_SR_TXCOMP) == TWI_SR_TXCOMP));
}

/*************************************************************************
 Wait for transmission to complete
*************************************************************************/
void HAL::i2cTxFinished(void)
{
    while(!((TWI_INTERFACE->TWI_SR & TWI_SR_TXRDY) == TWI_SR_TXRDY));
}

/*************************************************************************
  Send one byte to I2C device

  Input:    byte to be transfered
  Return:   0 write successful
            1 write failed
*************************************************************************/
unsigned char HAL::i2cWrite( uint8_t data )
{    
  i2cWriting(data);
  TWI_INTERFACE->TWI_CR = TWI_CR_STOP;
  i2cTxFinished();
  unsigned char rslt = (TWI_INTERFACE->TWI_SR & TWI_SR_NACK) == TWI_SR_NACK;
  i2cCompleted ();
  return rslt;
}

/*************************************************************************
  Send one byte to I2C device
  Transaction can continue with more writes or reads 
************************************************************************/
void HAL::i2cWriting( uint8_t data )
{    
    TWI_INTERFACE->TWI_THR = data;
}


/*************************************************************************
 Read one byte from the I2C device, request more data from device
 Return:  byte read from I2C device
*************************************************************************/
unsigned char HAL::i2cReadAck(void)
{
    while( !((TWI_INTERFACE->TWI_SR & TWI_SR_RXRDY) == TWI_SR_RXRDY) );
    return TWI_INTERFACE->TWI_RHR;
}

/*************************************************************************
 Read one byte from the I2C device, read is followed by a stop condition

 Return:  byte read from I2C device
*************************************************************************/
unsigned char HAL::i2cReadNak(void)
{
    TWI_INTERFACE->TWI_CR = TWI_CR_STOP;
    
    while( !((TWI_INTERFACE->TWI_SR & TWI_SR_RXRDY) == TWI_SR_RXRDY) );
    unsigned char data = i2cReadAck();
    i2cCompleted();
    return data;
}


#if FEATURE_SERVO
// may need further restrictions here in the future
#if defined (__SAM3X8E__)
#define SERVO2500US F_CPU_TRUE / 3200
#define SERVO5000US F_CPU_TRUE / 1600
unsigned int HAL::servoTimings[4] = {0,0,0,0};
static byte servoIndex = 0;
void HAL::servoMicroseconds(byte servo,int ms) {
    if(ms<500) ms = 0;
    if(ms>2500) ms = 2500;
    servoTimings[servo] = (unsigned int)(((F_CPU_TRUE / 1000000)*(long)ms)>>3);
}



// ================== Interrupt handling ======================

// Servo timer Interrupt handler
void SERVO_COMPA_VECTOR ()
{
  // apparently have to read status register
  TC_GetStatus(SERVO_TIMER, SERVO_TIMER_CHANNEL);

  switch(servoIndex) {
  case 0:
      TCNT3 = 0;
      if(HAL::servoTimings[0]) {
#if SERVO0_PIN>-1
        WRITE(SERVO0_PIN,HIGH);
#endif
        TC_SetRC(SERVO_TIMER, SERVO_TIMER_CHANNEL, HAL::servoTimings[0]);
      } else TC_SetRC(SERVO_TIMER, SERVO_TIMER_CHANNEL, SERVO2500US);
    break;
  case 1:
#if SERVO0_PIN>-1
      WRITE(SERVO0_PIN,LOW);
#endif
      TC_SetRC(SERVO_TIMER, SERVO_TIMER_CHANNEL, SERVO5000US);
    break;
  case 2:
      TCNT3 = 0;
      if(HAL::servoTimings[1]) {
#if SERVO1_PIN>-1
        WRITE(SERVO1_PIN,HIGH);
#endif
        TC_SetRC(SERVO_TIMER, SERVO_TIMER_CHANNEL, HAL::servoTimings[1]);
      } else TC_SetRC(SERVO_TIMER, SERVO_TIMER_CHANNEL, SERVO2500US);
    break;
  case 3:
#if SERVO1_PIN>-1
      WRITE(SERVO1_PIN,LOW);
#endif
      TC_SetRC(SERVO_TIMER, SERVO_TIMER_CHANNEL, SERVO5000US);
    break;
  case 4:
      TCNT3 = 0;
      if(HAL::servoTimings[2]) {
#if SERVO2_PIN>-1
        WRITE(SERVO2_PIN,HIGH);
#endif
        TC_SetRC(SERVO_TIMER, SERVO_TIMER_CHANNEL, HAL::servoTimings[2]);
      } else TC_SetRC(SERVO_TIMER, SERVO_TIMER_CHANNEL, SERVO2500US);
    break;
  case 5:
#if SERVO2_PIN>-1
      WRITE(SERVO2_PIN,LOW);
#endif
      TC_SetRC(SERVO_TIMER, SERVO_TIMER_CHANNEL, SERVO5000US);
    break;
  case 6:
      TCNT3 = 0;
      if(HAL::servoTimings[3]) {
#if SERVO3_PIN>-1
        WRITE(SERVO3_PIN,HIGH);
#endif
        TC_SetRC(SERVO_TIMER, SERVO_TIMER_CHANNEL, HAL::servoTimings[3]);
      } else TC_SetRC(SERVO_TIMER, SERVO_TIMER_CHANNEL, SERVO2500US);
    break;
  case 7:
#if SERVO3_PIN>-1
      WRITE(SERVO3_PIN,LOW);
#endif
      TC_SetRC(SERVO_TIMER, SERVO_TIMER_CHANNEL, SERVO5000US);
    break;
  }
  servoIndex++;
  if(servoIndex>7)
    servoIndex = 0;
}
#else
#error No servo support for your board, please diable FEATURE_SERVO
#endif
#endif

/** \brief Sets the timer 1 compare value to delay ticks.
*/
inline void setTimer(unsigned long delay)
{
    // convert old AVR timer delay value for SAM timers
    uint32_t timer_count = (delay * TIMER1_PRESCALE);

    if(timer_count == 0) timer_count = 1;
    TC_SetRC(TIMER1_TIMER, TIMER1_TIMER_CHANNEL, timer_count);
    TC_Start(TIMER1_TIMER, TIMER1_TIMER_CHANNEL);
}

/** \brief Timer interrupt routine to drive the stepper motors.
*/
void TIMER1_COMPA_VECTOR ()
{
    // apparently have to read status register
    TC_GetStatus(TIMER1_TIMER, TIMER1_TIMER_CHANNEL);
    if(insideTimer1) return;
    insideTimer1 = 1;
    if(PrintLine::hasLines())
    {
        setTimer(PrintLine::bresenhamStep());
        HAL::allowInterrupts();
    }
    else
    {
        if(waitRelax==0)
        {
#ifdef USE_ADVANCE
            if(Printer::advance_steps_set)
            {
                Printer::extruderStepsNeeded-=Printer::advance_steps_set;
#ifdef ENABLE_QUADRATIC_ADVANCE
                Printer::advance_executed = 0;
#endif
                Printer::advance_steps_set = 0;
            }
            if((!Printer::extruderStepsNeeded) && (DISABLE_E)) 
                Extruder::disableCurrentExtruderMotor();
#else
            if(DISABLE_E) Extruder::disableCurrentExtruderMotor();
#endif
        }
        else waitRelax--;
    }
    DEBUG_MEMORY;
    insideTimer1=0;
}

/**
This timer is called 3906 times per second. It is used to update
pwm values for heater and some other frequent jobs. 
*/
void PWM_TIMER_VECTOR ()
{
    // apparently have to read status register
    TC_GetStatus(PWM_TIMER, PWM_TIMER_CHANNEL);

    static byte pwm_count = 0;
    static byte pwm_pos_set[NUM_EXTRUDER+3];
    static byte pwm_cooler_pos_set[NUM_EXTRUDER];

    if(pwm_count==0)
    {
#if EXT0_HEATER_PIN>-1
        if((pwm_pos_set[0] = pwm_pos[0])>0) WRITE(EXT0_HEATER_PIN,1);
#if EXT0_EXTRUDER_COOLER_PIN>-1
        if((pwm_cooler_pos_set[0] = extruder[0].coolerPWM)>0) WRITE(EXT0_EXTRUDER_COOLER_PIN,1);
#endif
#endif
#if defined(EXT1_HEATER_PIN) && EXT1_HEATER_PIN>-1 && NUM_EXTRUDER>1
        if((pwm_pos_set[1] = pwm_pos[1])>0) WRITE(EXT1_HEATER_PIN,1);
#if EXT1_EXTRUDER_COOLER_PIN>-1
        if((pwm_cooler_pos_set[1] = extruder[1].coolerPWM)>0) WRITE(EXT1_EXTRUDER_COOLER_PIN,1);
#endif
#endif
#if defined(EXT2_HEATER_PIN) && EXT2_HEATER_PIN>-1 && NUM_EXTRUDER>2
        if((pwm_pos_set[2] = pwm_pos[2])>0) WRITE(EXT2_HEATER_PIN,1);
#if EXT2_EXTRUDER_COOLER_PIN>-1
        if((pwm_cooler_pos_set[2] = extruder[2].coolerPWM)>0) WRITE(EXT2_EXTRUDER_COOLER_PIN,1);
#endif
#endif
#if defined(EXT3_HEATER_PIN) && EXT3_HEATER_PIN>-1 && NUM_EXTRUDER>3
        if((pwm_pos_set[3] = pwm_pos[3])>0) WRITE(EXT3_HEATER_PIN,1);
#if EXT3_EXTRUDER_COOLER_PIN>-1
        if((pwm_cooler_pos_set[3] = extruder[3].coolerPWM)>0) WRITE(EXT3_EXTRUDER_COOLER_PIN,1);
#endif
#endif
#if defined(EXT4_HEATER_PIN) && EXT4_HEATER_PIN>-1 && NUM_EXTRUDER>4
        if((pwm_pos_set[4] = pwm_pos[4])>0) WRITE(EXT4_HEATER_PIN,1);
#if EXT4_EXTRUDER_COOLER_PIN>-1
        if((pwm_cooler_pos_set[4] = pwm_pos[4].coolerPWM)>0) WRITE(EXT4_EXTRUDER_COOLER_PIN,1);
#endif
#endif
#if defined(EXT5_HEATER_PIN) && EXT5_HEATER_PIN>-1 && NUM_EXTRUDER>5
        if((pwm_pos_set[5] = pwm_pos[5])>0) WRITE(EXT5_HEATER_PIN,1);
#if EXT5_EXTRUDER_COOLER_PIN>-1
        if((pwm_cooler_pos_set[5] = extruder[5].coolerPWM)>0) WRITE(EXT5_EXTRUDER_COOLER_PIN,1);
#endif
#endif
#if FAN_BOARD_PIN>-1
        if((pwm_pos_set[NUM_EXTRUDER+1] = pwm_pos[NUM_EXTRUDER+1])>0) WRITE(FAN_BOARD_PIN,1);
#endif
#if FAN_PIN>-1 && FEATURE_FAN_CONTROL
        if((pwm_pos_set[NUM_EXTRUDER+2] = pwm_pos[NUM_EXTRUDER+2])>0) WRITE(FAN_PIN,1);
#endif
#if HEATED_BED_HEATER_PIN>-1 && HAVE_HEATED_BED
        if((pwm_pos_set[NUM_EXTRUDER] = pwm_pos[NUM_EXTRUDER])>0) WRITE(HEATED_BED_HEATER_PIN,1);
#endif
    }
#if EXT0_HEATER_PIN>-1
    if(pwm_pos_set[0] == pwm_count && pwm_pos_set[0]!=255) WRITE(EXT0_HEATER_PIN,0);
#if EXT0_EXTRUDER_COOLER_PIN>-1
    if(pwm_cooler_pos_set[0] == pwm_count && pwm_cooler_pos_set[0]!=255) WRITE(EXT0_EXTRUDER_COOLER_PIN,0);
#endif
#endif
#if defined(EXT1_HEATER_PIN) && EXT1_HEATER_PIN>-1 && NUM_EXTRUDER>1
    if(pwm_pos_set[1] == pwm_count && pwm_pos_set[1]!=255) WRITE(EXT1_HEATER_PIN,0);
#if EXT1_EXTRUDER_COOLER_PIN>-1
    if(pwm_cooler_pos_set[1] == pwm_count && pwm_cooler_pos_set[1]!=255) WRITE(EXT1_EXTRUDER_COOLER_PIN,0);
#endif
#endif
#if defined(EXT2_HEATER_PIN) && EXT2_HEATER_PIN>-1 && NUM_EXTRUDER>2
    if(pwm_pos_set[2] == pwm_count && pwm_pos_set[2]!=255) WRITE(EXT2_HEATER_PIN,0);
#if EXT2_EXTRUDER_COOLER_PIN>-1
    if(pwm_cooler_pos_set[2] == pwm_count && pwm_cooler_pos_set[2]!=255) WRITE(EXT2_EXTRUDER_COOLER_PIN,0);
#endif
#endif
#if defined(EXT3_HEATER_PIN) && EXT3_HEATER_PIN>-1 && NUM_EXTRUDER>3
    if(pwm_pos_set[3] == pwm_count && pwm_pos_set[3]!=255) WRITE(EXT3_HEATER_PIN,0);
#if EXT3_EXTRUDER_COOLER_PIN>-1
    if(pwm_cooler_pos_set[3] == pwm_count && pwm_cooler_pos_set[3]!=255) WRITE(EXT3_EXTRUDER_COOLER_PIN,0);
#endif
#endif
#if defined(EXT4_HEATER_PIN) && EXT4_HEATER_PIN>-1 && NUM_EXTRUDER>4
    if(pwm_pos_set[4] == pwm_count && pwm_pos_set[4]!=255) WRITE(EXT4_HEATER_PIN,0);
#if EXT4_EXTRUDER_COOLER_PIN>-1
    if(pwm_cooler_pos_set[4] == pwm_count && pwm_cooler_pos_set[4]!=255) WRITE(EXT4_EXTRUDER_COOLER_PIN,0);
#endif
#endif
#if defined(EXT5_HEATER_PIN) && EXT5_HEATER_PIN>-1 && NUM_EXTRUDER>5
    if(pwm_pos_set[5] == pwm_count && pwm_pos_set[5]!=255) WRITE(EXT5_HEATER_PIN,0);
#if EXT5_EXTRUDER_COOLER_PIN>-1
    if(pwm_cooler_pos_set[5] == pwm_count && pwm_cooler_pos_set[5]!=255) WRITE(EXT5_EXTRUDER_COOLER_PIN,0);
#endif
#endif
#if FAN_BOARD_PIN>-1
    if(pwm_pos_set[NUM_EXTRUDER+2] == pwm_count && pwm_pos_set[NUM_EXTRUDER+2]!=255) WRITE(FAN_BOARD_PIN,0);
#endif
#if FAN_PIN>-1 && FEATURE_FAN_CONTROL
    if(pwm_pos_set[NUM_EXTRUDER+2] == pwm_count && pwm_pos_set[NUM_EXTRUDER+2]!=255) WRITE(FAN_PIN,0);
#endif
#if HEATED_BED_HEATER_PIN>-1 && HAVE_HEATED_BED
    if(pwm_pos_set[NUM_EXTRUDER] == pwm_count && pwm_pos_set[NUM_EXTRUDER]!=255) WRITE(HEATED_BED_HEATER_PIN,0);
#endif
    HAL::allowInterrupts();
    counter_periodical++; // Appxoimate a 100ms timer
    if(counter_periodical >= 390) //  (int)(F_CPU/40960))
    {
        counter_periodical=0;
        execute_periodical=1;
    }
// read analog values -- only read one per interrupt
#if ANALOG_INPUTS>0
        
    // conversion finished?
    //if(ADC->ADC_ISR & ADC_ISR_EOC(adcChannel[osAnalogInputPos])) 
    if(ADC->ADC_ISR & ADC_ISR_EOC(osAnalogInputChannels[osAnalogInputPos])) 
    {                
      //osAnalogInputChannels
        //osAnalogInputBuildup[osAnalogInputPos] += ADC->ADC_CDR[adcChannel[osAnalogInputPos]]; 
        osAnalogInputBuildup[osAnalogInputPos] += ADC->ADC_CDR[osAnalogInputChannels[osAnalogInputPos]]; 
        if(++osAnalogInputCounter[osAnalogInputPos] >= (1 << ANALOG_INPUT_SAMPLE))
        {
#if ANALOG_INPUT_BITS+ANALOG_INPUT_SAMPLE<12
            osAnalogInputValues[osAnalogInputPos] =
                osAnalogInputBuildup[osAnalogInputPos] <<
                (12-ANALOG_INPUT_BITS-ANALOG_INPUT_SAMPLE);
#endif
#if ANALOG_INPUT_BITS+ANALOG_INPUT_SAMPLE>12
            osAnalogInputValues[osAnalogInputPos] =
                osAnalogInputBuildup[osAnalogInputPos] >>
                (ANALOG_INPUT_BITS+ANALOG_INPUT_SAMPLE-12);
#endif
#if ANALOG_INPUT_BITS+ANALOG_INPUT_SAMPLE==12
            osAnalogInputValues[osAnalogInputPos] =
                osAnalogInputBuildup[osAnalogInputPos];
#endif
            osAnalogInputBuildup[osAnalogInputPos] = 0;
            osAnalogInputCounter[osAnalogInputPos] = 0;
        }
        // Start next conversion cycle
        if(++osAnalogInputPos>=ANALOG_INPUTS) { 
            osAnalogInputPos = 0;
            ADC->ADC_CR = ADC_CR_START;
        }
    }
#endif
    UI_FAST; // Short timed user interface action
    pwm_count++;
}

#if defined(USE_ADVANCE)
byte extruder_wait_dirchange=0; ///< Wait cycles, if direction changes. Prevents stepper from loosing steps.
char extruder_last_dir = 0;
byte extruder_speed = 0;
#endif

/** \brief Timer routine for extruder stepper.

Several methods need to move the extruder. To get a optimal 
result, all methods update the printer_state.extruderStepsNeeded 
with the number of additional steps needed. During this 
interrupt, one step is executed. This will keep the extruder 
moving, until the total wanted movement is achieved. This will 
be done with the maximum allowable speed for the extruder. 
*/
#if defined(USE_ADVANCE)
// EXTRUDER_TIMER IRQ handler
void EXTRUDER_TIMER_VECTOR ()
{
    // apparently have to read status register
    TC_GetStatus(EXTRUDER_TIMER, EXTRUDER_TIMER_CHANNEL);

    if(!Printer::isAdvanceActivated()) return; // currently no need

    // get current extruder timer count value
    uint32_t timer = EXTRUDER_TIMER->TC_CHANNEL[EXTRUDER_TIMER_CHANNEL].TC_RC;

    // have to convert old AVR delay values for Due timers
    timer += Printer::maxExtruderSpeed; // / (F_CPU_TRUE / F_CPU);
    bool increasing = Printer::extruderStepsNeeded>0;

    // Require at least 2 steps in one direction before going to action
    if(abs(Printer::extruderStepsNeeded)<2)
    {
        TC_SetRC(EXTRUDER_TIMER, EXTRUDER_TIMER_CHANNEL, timer);
        ANALYZER_OFF(ANALYZER_CH2);
        extruder_last_dir = 0;
        return;
    }

    if(extruder_last_dir==0)
    {
        Extruder::setDirection(increasing ? 1 : 0);
        extruder_last_dir = (increasing ? 1 : -1);
    }
    Extruder::step();
    Printer::extruderStepsNeeded-=extruder_last_dir;
#if STEPPER_HIGH_DELAY>0
    HAL::delayMicroseconds(STEPPER_HIGH_DELAY);
#endif
    Extruder::unstep();

    TC_SetRC(EXTRUDER_TIMER, EXTRUDER_TIMER_CHANNEL, timer);
}
#endif

// IRQ handler for tone generator
void BEEPER_TIMER_VECTOR () {
    static bool     toggle;

    TC_GetStatus(BEEPER_TIMER, BEEPER_TIMER_CHANNEL);

    WRITE(tone_pin, toggle);
    toggle = !toggle;
}












