#include <ti/devices/msp432p4xx/driverlib/driverlib.h>

/* Standard Includes */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define PWM_PERIOD      20000   // 20ms period (1MHz clock)
#define SERVO_MIN       400     // less than 1ms pulse (0 degrees)
#define SERVO_MAX       2600    // more than 2ms pulse (about 180 degrees)
#define SERVO_MID       1400    // 1.4-1.5ms pulse (neutral position)
#define LEFT            1900
#define RIGHT           1100
#define UP              1000
#define DOWN            1500

typedef enum {
    STATE_CENTER,
    STATE_LEFT,
    STATE_RIGHT,
    STATE_UP,
    STATE_DOWN,
    NUM_STATES
} States;

typedef struct {
    States state;
    void (*stateFunction)(void);
} StateMachine;

void initPWM(void);
void initUART(void);
void setServoPosition(uint8_t servoNum, uint16_t position);
void moveServos(States state);
void fn_center(void);
void fn_left(void);
void fn_right(void);
void fn_up(void);
void fn_down(void);
void sendChar(char TXData);

StateMachine fsm[] = {
                      {STATE_CENTER, fn_center},
                      {STATE_LEFT, fn_left},
                      {STATE_RIGHT, fn_right},
                      {STATE_UP, fn_up},
                      {STATE_DOWN, fn_down}
};

char RXData;
volatile States current = STATE_CENTER;
volatile States prev = STATE_CENTER;
bool updated=true;

const eUSCI_UART_ConfigV1 uartConfig =
{
        EUSCI_A_UART_CLOCKSOURCE_SMCLK,                 // SMCLK Clock Source, 3MHz
        6,                                              // BRDIV
        8,                                              // UCxBRF
        32,                                             // UCxBRS
        EUSCI_A_UART_NO_PARITY,                         // No Parity
        EUSCI_A_UART_LSB_FIRST,                         // MSB First or LSB
        EUSCI_A_UART_ONE_STOP_BIT,                      // One stop bit
        EUSCI_A_UART_MODE,                              // UART mode
        EUSCI_A_UART_OVERSAMPLING_BAUDRATE_GENERATION,  // Oversampling
        EUSCI_A_UART_8_BIT_LEN                          // 8 bit data length
};
//eUSCI baud rate configurations
//3 MHz, 9600: 19, 8, 85
//3 MHz 115200: 1, 10, 0
//12 MHz 9600: 78, 2, 0
//12 MHz 115200: 6, 8, 32
//24 MHz 9600: 156, 4, 0
//24 MHz 115200: 13, 0, 37


int main(void)
{
    /* Halting WDT  */
    WDT_A_holdTimer();
    /* MCLK = HSMCLK = SMCLK = DCO of 3MHz*/
    CS_setDCOCenteredFrequency(CS_DCO_FREQUENCY_12);  //set DCO to 12 MHz
    initUART();
    initPWM();
    while(1)
    {
        Interrupt_enableSleepOnIsrExit();
        PCM_gotoLPM0InterruptSafe();
    }
}

void EUSCIA2_IRQHandler(void) {
    uint32_t status = UART_getEnabledInterruptStatus(EUSCI_A2_BASE);
    GPIO_setOutputHighOnPin(GPIO_PORT_P1, GPIO_PIN0);
    if(status & EUSCI_A_UART_RECEIVE_INTERRUPT_FLAG)
    {
        RXData = UART_receiveData(EUSCI_A2_BASE);
        if(RXData != '\n'){
            switch(RXData){
                 case 'C':
                     prev=current;
                     current = STATE_CENTER;
                     break;
                 case 'L':
                     prev=current;
                     current = STATE_LEFT;
                     break;
                 case 'R':
                     prev=current;
                     current = STATE_RIGHT;
                     break;
                 case 'U':
                     prev=current;
                     current = STATE_UP;
                     break;
                 case 'D':
                     prev=current;
                     current = STATE_DOWN;
                     break;
                 default: //ignore
                     break;
            }
            updated=false;
            //printf("%c\n", RXData);  //debug
        }
        if(!updated)
              moveServos(current);
        Interrupt_disableSleepOnIsrExit();
    }
}

void fn_center(void) {
    setServoPosition(1, SERVO_MID);
    setServoPosition(2, SERVO_MID - 200);
}

void fn_left(void) {
    setServoPosition(1, LEFT);
}

void fn_right(void) {
    setServoPosition(1, RIGHT);
}

void fn_up(void) {
    setServoPosition(2, UP);
}

void fn_down(void) {
    setServoPosition(2, DOWN);
}

void moveServos(States state){
    if(state == STATE_LEFT && prev == STATE_RIGHT) state = STATE_CENTER;
    if(state == STATE_RIGHT && prev == STATE_LEFT) state = STATE_CENTER;
    if(state == STATE_UP && prev == STATE_DOWN) state = STATE_CENTER;
    if(state == STATE_DOWN && prev == STATE_UP) state = STATE_CENTER;
    //if(state == 'C' && prev == 'L' || prev == '')
    //__disable_irq();
    if(state < NUM_STATES){
         (*fsm[state].stateFunction)();
    }
    //__enable_irq();
    updated=true;
}

void setServoPosition(uint8_t servoNum, uint16_t position) {
    if (position < SERVO_MIN) position = SERVO_MIN;
    if (position > SERVO_MAX) position = SERVO_MAX;

    if (servoNum == 1) {
        Timer_A_setCompareValue(TIMER_A0_BASE, TIMER_A_CAPTURECOMPARE_REGISTER_1, position);
    } else if (servoNum == 2) {
        Timer_A_setCompareValue(TIMER_A0_BASE, TIMER_A_CAPTURECOMPARE_REGISTER_2, position);
    }
}

void sendChar(char TXData){
    UART_transmitData(EUSCI_A2_BASE, TXData);
}

void initUART(void) {
    GPIO_setAsPeripheralModuleFunctionInputPin(GPIO_PORT_P3, GPIO_PIN2 | GPIO_PIN3, GPIO_PRIMARY_MODULE_FUNCTION);
    //RED LED
    GPIO_setAsOutputPin(GPIO_PORT_P1, GPIO_PIN0);
    GPIO_setOutputLowOnPin(GPIO_PORT_P1, GPIO_PIN0);
    //GREEN LED
    GPIO_setAsOutputPin(GPIO_PORT_P2, GPIO_PIN1);
    GPIO_setOutputLowOnPin(GPIO_PORT_P2, GPIO_PIN1);

    /* Configuring UART Module */
    UART_initModule(EUSCI_A2_BASE, &uartConfig);

    /* Enable UART module */
    UART_enableModule(EUSCI_A2_BASE);

    /* Enabling interrupts */
    UART_enableInterrupt(EUSCI_A2_BASE, EUSCI_A_UART_RECEIVE_INTERRUPT);
    Interrupt_enableInterrupt(INT_EUSCIA2);
    Interrupt_enableSleepOnIsrExit();
}

void initPWM(void) {
    // Configure P2.4 (TA0.1) and P2.5 (TA0.2) for PWM output
    GPIO_setAsPeripheralModuleFunctionOutputPin(
        GPIO_PORT_P2,
        GPIO_PIN4 | GPIO_PIN5,
        GPIO_PRIMARY_MODULE_FUNCTION
    );

    // Configure Timer_A0 for PWM
    Timer_A_UpModeConfig timerConfig = {
        TIMER_A_CLOCKSOURCE_SMCLK,            // SMCLK = 3MHz
        TIMER_A_CLOCKSOURCE_DIVIDER_12,        // 3MHz / 3 = 1MHz
        PWM_PERIOD,                           // 20ms period
        TIMER_A_TAIE_INTERRUPT_DISABLE,       // Disable timer interrupt
        TIMER_A_CCIE_CCR0_INTERRUPT_DISABLE,  // Disable CCR0 interrupt
        TIMER_A_DO_CLEAR                      // Clear timer counter
    };
    Timer_A_configureUpMode(TIMER_A0_BASE, &timerConfig);

    // Configure CCR1 for Servo 1 (P2.4)
    Timer_A_CompareModeConfig compareConfig1 = {
        TIMER_A_CAPTURECOMPARE_REGISTER_1,
        TIMER_A_CAPTURECOMPARE_INTERRUPT_DISABLE,
        TIMER_A_OUTPUTMODE_RESET_SET,         // PWM output mode
        SERVO_MID                             // Initial position (1.5ms)
    };
    Timer_A_initCompareMode(TIMER_A0_BASE, &compareConfig1);

    // Configure CCR2 for Servo 2 (P2.5)
    Timer_A_CompareModeConfig compareConfig2 = {
        TIMER_A_CAPTURECOMPARE_REGISTER_2,
        TIMER_A_CAPTURECOMPARE_INTERRUPT_DISABLE,
        TIMER_A_OUTPUTMODE_RESET_SET,         // PWM output mode
        SERVO_MID                             // Initial position (1.5ms)
    };
    Timer_A_initCompareMode(TIMER_A0_BASE, &compareConfig2);

    // Start Timer_A0
    Timer_A_startCounter(TIMER_A0_BASE, TIMER_A_UP_MODE);
}
