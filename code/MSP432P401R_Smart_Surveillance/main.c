/*-----MSP432P401R_Smart_Surveillance-----*/

/*-----Libraries included-----*/
#include <ti/devices/msp432p4xx/driverlib/driverlib.h>  // Official Texas Instruments MSP432 driver library with sets of functional APIs
#include <stdint.h>                                     // Standard libraries
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>


/*-----Global definitions-----*/
// Constants
#define PWM_PERIOD      20000   // 20ms (1MHz CLK)
#define SERVO_MIN       400     // Servomotor degree limit: less than 1ms pulse (0°)                      
#define SERVO_MAX       2600    // Servomotor degree limit: more than 2ms pulse (around 180°)
#define SERVO_MID       1400    // Servomotor neutral centered position: 1.4 ~ 1.5ms pulse
#define LEFT            1900    // Servomotor left position
#define RIGHT           1100    // Servomotor right position
#define UP              1000    // Servomotor up position
#define DOWN            1500    // Servomotor down position

// Variables
char RXData;
volatile States current = STATE_CENTER;  // Starting conditions
volatile States prev = STATE_CENTER;

// Customizable UART configuration
const eUSCI_UART_ConfigV1 uartConfig = {
        EUSCI_A_UART_CLOCKSOURCE_SMCLK,                 // CLK Source: SMCLK (12MHz)
        6,                                              // BRDIV (Prescalers)
        8,                                              // UCxBRF
        32,                                             // UCxBRS
        EUSCI_A_UART_NO_PARITY,                         // Parity mode: No parity
        EUSCI_A_UART_LSB_FIRST,                         // First MSB/LSB: LSB
        EUSCI_A_UART_ONE_STOP_BIT,                      // Stop bit configuration: One stop bit
        EUSCI_A_UART_MODE,                              // Mode: UART
        EUSCI_A_UART_OVERSAMPLING_BAUDRATE_GENERATION,  // Oversampling: Enabled
        EUSCI_A_UART_8_BIT_LEN                          // Data length: 8 bits
};
// eUSCI baud rate configurations:
// - 3  MHz, 9600    ->  19,    8,   85
// - 3  MHz, 115200  ->   1,   10,    0
// - 12 MHz, 9600    ->  78,    2,    0
// - 12 MHz, 115200  ->   6,    8,   32
// - 24 MHz, 9600    -> 156,    4,    0
// - 24 MHz, 115200  ->  13,    0,   37

/*-----Function headers-----*/
//General functions
void initPWM(void);
void initUART(void);
void setServoPosition(uint8_t servoNum, uint16_t position);
void moveServos(States state);
//void sendChar(char TXData);

//FSM functions
void fn_center(void);
void fn_left(void);
void fn_right(void);
void fn_up(void);
void fn_down(void);


/*-----Building the FSM-----*/
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

StateMachine fsm[] = {
                     {STATE_CENTER, fn_center},
                     {STATE_LEFT, fn_left},
                     {STATE_RIGHT, fn_right},
                     {STATE_UP, fn_up},
                     {STATE_DOWN, fn_down}
};


/*-----Main loop-----*/
int main(void) {
    // Setup
    WDT_A_holdTimer();     // Halting WDT

    // MCLK = HSMCLK = SMCLK = DCO of 12MHz
    CS_setDCOCenteredFrequency(CS_DCO_FREQUENCY_12);  // Sets DCO to 12 MHz
    initUART();              // Initialize UART
    initPWM();               // Initialize PWM

    while(1) {                              // Infinite loop: enters sleep mode
        Interrupt_enableSleepOnIsrExit();
        PCM_gotoLPM0InterruptSafe();
    }
}


/*-----Interrupt handlers-----*/
void EUSCIA2_IRQHandler(void) {
    uint32_t status = UART_getEnabledInterruptStatus(EUSCI_A2_BASE);
    GPIO_setOutputHighOnPin(GPIO_PORT_P1, GPIO_PIN0);                   // Red LED on

    if(status & EUSCI_A_UART_RECEIVE_INTERRUPT_FLAG) {
        RXData = UART_receiveData(EUSCI_A2_BASE);       // Receives the state from ESP32CAM

        if(RXData != '\n'){                    // Selects camera position based on the data received
            prev = current;
            switch(RXData){
                case 'C':   current = STATE_CENTER;             break;
                case 'L':   current = STATE_LEFT;               break;
                case 'R':   current = STATE_RIGHT;              break;
                case 'U':   current = STATE_UP;                 break;
                case 'D':   current = STATE_DOWN;               break; 
                default:    Interrupt_disableSleepOnIsrExit();  return;
            }
                
            moveServos(current);
          //printf("%c\n", RXData);     // Debug
        }

        Interrupt_disableSleepOnIsrExit();
    }
}


/*-----FSM function bodies-----*/
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


/*-----Function bodies-----*/

// Decides where to move the ESP32CAM into desired position an moves the servos
void moveServos(States state){
    // Confirms servomotors positioning by considering both current state and previous state
    if(state == STATE_LEFT && prev == STATE_RIGHT)  state = STATE_CENTER;
    if(state == STATE_RIGHT && prev == STATE_LEFT)  state = STATE_CENTER;
    if(state == STATE_UP && prev == STATE_DOWN)     state = STATE_CENTER;
    if(state == STATE_DOWN && prev == STATE_UP)     state = STATE_CENTER;

  //__disable_irq();
    if(state < NUM_STATES){
        (*fsm[state].stateFunction)();  // Performs the FSM function (moves the servos)
    }
  //__enable_irq();
}

// Positions servomotors in desired position
void setServoPosition(uint8_t servoNum, uint16_t position) {
    if(position < SERVO_MIN)    position = SERVO_MIN;    // Avoids servos to rotate past their mechanical limits
    if(position > SERVO_MAX)    position = SERVO_MAX;

    if(servoNum == 1) {                                                                         // 1 = Servo for x-axis (L - R)
        Timer_A_setCompareValue(TIMER_A0_BASE, TIMER_A_CAPTURECOMPARE_REGISTER_1, position);
    }else if(servoNum == 2) {                                                                   // 2 = Servo for y-axis (U - D)
        Timer_A_setCompareValue(TIMER_A0_BASE, TIMER_A_CAPTURECOMPARE_REGISTER_2, position);
    }
}


// Debug: Transmits data
//void sendChar(char TXData){
//    UART_transmitData(EUSCI_A2_BASE, TXData);
//}


// Initializes UART communication
void initUART(void) {
    GPIO_setAsPeripheralModuleFunctionInputPin(GPIO_PORT_P3, GPIO_PIN2 | GPIO_PIN3, GPIO_PRIMARY_MODULE_FUNCTION);

    GPIO_setAsOutputPin(GPIO_PORT_P1, GPIO_PIN0);       
    GPIO_setOutputLowOnPin(GPIO_PORT_P1, GPIO_PIN0);    // Red LED off

    UART_initModule(EUSCI_A2_BASE, &uartConfig);                            // Sets customized configuration
    UART_enableModule(EUSCI_A2_BASE);                                       // Enables UART
    UART_enableInterrupt(EUSCI_A2_BASE, EUSCI_A_UART_RECEIVE_INTERRUPT);    // Enables interrupts
    Interrupt_enableInterrupt(INT_EUSCIA2);
    Interrupt_enableSleepOnIsrExit();
}


// Initializes timer for PWM control
void initPWM(void) {
    // Selects P2.4 (TA0.1) and P2.5 (TA0.2) for PWM output
    GPIO_setAsPeripheralModuleFunctionOutputPin(
            GPIO_PORT_P2,
            GPIO_PIN4 | GPIO_PIN5,
            GPIO_PRIMARY_MODULE_FUNCTION
    );

    // Timer_A0 configuration for PWM control
    Timer_A_UpModeConfig timerConfig = {
                                       TIMER_A_CLOCKSOURCE_SMCLK,            // CLK Source SMCLK = 12MHz
                                       TIMER_A_CLOCKSOURCE_DIVIDER_12,       // Prescaler: 12MHz / 12 = 1MHz
                                       PWM_PERIOD,                           // Timer period (20ms)
                                       TIMER_A_TAIE_INTERRUPT_DISABLE,       // Enable timer interrupt: NO
                                       TIMER_A_CCIE_CCR0_INTERRUPT_DISABLE,  // Enable CCR0 interrupt: NO
                                       TIMER_A_DO_CLEAR                      // Enable timer counter clearer: YES
    };
    Timer_A_configureUpMode(TIMER_A0_BASE, &timerConfig);

    // CCR1 configuration for Servo 1 (P2.4) --- COMPARE MODE ---
    Timer_A_CompareModeConfig compareConfig1 = {
                                               TIMER_A_CAPTURECOMPARE_REGISTER_1,           // Selects CCR1
                                               TIMER_A_CAPTURECOMPARE_INTERRUPT_DISABLE,    // Enable interrupt: NO
                                               TIMER_A_OUTPUTMODE_RESET_SET,                // Output mode: PWM
                                               SERVO_MID                                    // Initial pulse width (1.5ms)
    };
    Timer_A_initCompareMode(TIMER_A0_BASE, &compareConfig1);

    // CCR2 configuration for Servo 2 (P2.5) --- COMPARE MODE ---
    Timer_A_CompareModeConfig compareConfig2 = {
                                                TIMER_A_CAPTURECOMPARE_REGISTER_2,           // Selects CCR2
                                                TIMER_A_CAPTURECOMPARE_INTERRUPT_DISABLE,    // Enable interrupt: NO
                                                TIMER_A_OUTPUTMODE_RESET_SET,                // Output mode: PWM
                                                SERVO_MID                                    // Initial pulse width (1.5ms)
    };
    Timer_A_initCompareMode(TIMER_A0_BASE, &compareConfig2);

    Timer_A_startCounter(TIMER_A0_BASE, TIMER_A_UP_MODE);       // Starts Timer_A0 counting
}
