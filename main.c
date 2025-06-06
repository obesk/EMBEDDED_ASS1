#include "timer.h"
#include "uart.h"
#include "spi.h"
#include "parser.h"

#include <xc.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

// Since we use a 10 bit UART transmission we use 10 bits for a byte of data. 
// With a 100Hz main we have 9,6 bytes per cycle
#define INPUT_BUFF_LEN 10

// considerations on MAG message:
// for the x and y axis we have 2^13 bytes signed, which is equivalent to range:
// 4095 : -4096  -> 5 bytes max
// for the z axis we have 2^15 bytes signed, which is equivalent to range:
// 16383 : -16384 -> 6 bytes max

// considreations on YAW message:
// the yaw is in degrees so we have a range of 180 : -180 -> 4 bytes

// considerations on ERR from rate message:
// with 10 bytes we can have a max of 2 RATE messages received
// each ERR message is 7 bytes so 14 bytes max

// considering the worst can in which we print all messages together:
// - $MAG,,,* -> 8 bytes
// - x, y axis -> 10 bytes max
// - z axis -> 6 bytes max
// - $YAW,* -> 6 bytes
// - angle -> 4 bytes
// - err message -> 14 bytes
// total: 48 bytes
#define OUTPUT_BUFF_LEN 48

// this define the frequency of the tasks based on the frequency of the main.
#define CLOCK_LD_TOGGLE 50 // led2 blinking at 1Hz
#define CLOCK_ACQUIRE_MAG 4 // acquiring magnetometer values at 25Hz
#define CLOCK_YAW_PRINT 20 // printing yaw at 5Hz

#define N_MAG_READINGS 5 // number of mag values to keep for the average

#define VALID_RATES_N 6

char input_buff[INPUT_BUFF_LEN];
char output_buff[OUTPUT_BUFF_LEN];

struct circular_buffer UART_input_buff = {
    .len = INPUT_BUFF_LEN,
    .buff = input_buff,
};

struct circular_buffer UART_output_buff = {
    .len = OUTPUT_BUFF_LEN,
    .buff = output_buff,
};

// to avoid overflow with sums it's better to use long
struct MagReading {
    long x;
    long y;
    long z;
};

struct MagReadings {
    int w;
    struct MagReading readings[N_MAG_READINGS];
};

enum Axis {
    X_AXIS = 0,
    Y_AXIS,
    Z_AXIS,
};

void algorithm() {
    tmr_wait_ms(TIMER2, 7);
}

const int valid_rates_values[] = {0, 1, 2, 4, 5, 10};

int is_valid_rate(int rate) {
    for(int i = 0; i < VALID_RATES_N; i++){
        if(valid_rates_values[i] == rate){
            return 1;
        }
    }
    return 0;
}

void activate_magnetometer() {
    //selecting the magnetometer and disabling accelerometer and gyroscope
    CS_ACC = 1;
    CS_GYR = 1;
    
    CS_MAG = 0;
    spi_write(0x4B);
    spi_write(0x01); // changing the magnetometer to sleep state
    CS_MAG = 1;

    tmr_wait_ms(TIMER1, 3); //waiting for the magnetometer to go into sleep state
    
    CS_MAG = 0;
    spi_write(0x4C);
    spi_write(0x00); // changing the magnetometer to active state  
    CS_MAG = 1;

    tmr_wait_ms(TIMER1, 3); //waiting for the magnetometer to go into active state
}

int read_mag_axis(enum Axis axis) {
    int axis_value;

    // the overflow should not happen by design. If it happens the LED1 is turned
    // on to signal a bug in the code
    if(SPI1STATbits.SPIROV){
        SPI1STATbits.SPIROV = 0;
        LATA = 1;
    }
    CS_MAG = 0;
    // the axis registers are sequential
    spi_write((0x42 + axis * 2)| 0x80); //writing the axis register to read

    if (axis == X_AXIS || axis == Y_AXIS){
        // converting to int and shifting the values
        const int bytes_value = (spi_write(0x00) & 0x00F8) | (spi_write(0x00) << 8);
        axis_value = bytes_value >> 3;
    } else {
        // converting to int and shifting the values
        const int bytes_value = (spi_write(0x00) & 0x00FE) | (spi_write(0x00) << 8);
        axis_value = bytes_value >> 1;
    }

    CS_MAG = 1;
    return axis_value;
}

int main(void) {
    init_uart();
    init_spi();

    int print_mag_rate = 5;

    struct MagReadings mag_readings = {0};

    UART_input_buff.buff = input_buff;
    UART_output_buff.buff = output_buff;

    TRISA = TRISG = 0x0000; // setting port A and G as output
    ANSELA = ANSELB = ANSELC = ANSELD = ANSELE = ANSELG = 0x0000; // disabling analog function

    activate_magnetometer();

    // our largerst string is 20 bytes, this should be changed in case of differnt print messages
    char output_str [20]; 

    int LD2_toggle_counter = 0;
    int acquire_mag_counter = 0;
    int print_mag_counter = 0;
    int print_yaw_counter = 0;


    struct MagReading avg_reading = {0};
    int yaw_deg = 0;

    parser_state pstate = {.state = STATE_DOLLAR };

    // filling the array of magnetormeter readings to ensure that the first 
    // average value computed is right
    tmr_setup_period(TIMER1, 40); // setting the same period as in the main
    for (int i = 0; i < N_MAG_READINGS; ++i) {
        mag_readings.readings[i] = (struct MagReading) {
            .x = read_mag_axis(X_AXIS),
            .y = read_mag_axis(Y_AXIS),
            .z = read_mag_axis(Z_AXIS),
        };
        tmr_wait_period(TIMER1);
    }

    const int main_hz = 100;
    tmr_setup_period(TIMER1, 1000 / main_hz); // 100 Hz frequency

    while (1) {
        algorithm();
        if (++LD2_toggle_counter >= CLOCK_LD_TOGGLE) {
            LD2_toggle_counter = 0;
            LATGbits.LATG9 = !LATGbits.LATG9;
        }

        if (++acquire_mag_counter >= CLOCK_ACQUIRE_MAG) {
            acquire_mag_counter = 0;

            mag_readings.readings[mag_readings.w] = (struct MagReading) {
                .x = read_mag_axis(X_AXIS),
                .y = read_mag_axis(Y_AXIS),
                .z = read_mag_axis(Z_AXIS),
            };

            mag_readings.w = (mag_readings.w + 1) % N_MAG_READINGS;

            struct MagReading sum_reading = {0}; 
            for(int i = mag_readings.w ; i != (mag_readings.w + 1) % N_MAG_READINGS; i = (i + 1) % N_MAG_READINGS) {
                sum_reading.x += mag_readings.readings[i].x;
                sum_reading.y += mag_readings.readings[i].y;
                sum_reading.z += mag_readings.readings[i].z;
            }

            avg_reading.x = sum_reading.x / N_MAG_READINGS,
            avg_reading.y = sum_reading.y / N_MAG_READINGS,
            avg_reading.z = sum_reading.z / N_MAG_READINGS,

            yaw_deg = (int) (180.0 * atan2((float)avg_reading.y, (float)avg_reading.x) / M_PI);
        }

        if (print_mag_rate && ++print_mag_counter >= (main_hz / print_mag_rate)) {
            print_mag_counter = 0;
            sprintf(output_str, "$MAG,%d,%d,%d*", (int)avg_reading.x, (int)avg_reading.y, (int)avg_reading.z);
            print_to_buff(output_str, &UART_output_buff);
        }

        if (++print_yaw_counter >= CLOCK_YAW_PRINT) {
            print_yaw_counter = 0;
            sprintf(output_str, "$YAW,%d*", yaw_deg); 
            print_to_buff(output_str, &UART_output_buff);
        }

        while(UART_input_buff.read != UART_input_buff.write) {
            const int status = parse_byte(&pstate, UART_input_buff.buff[UART_input_buff.read]);
            if(status == NEW_MESSAGE) {
                if(strcmp(pstate.msg_type, "RATE") == 0) {
                    const int rate = extract_integer(pstate.msg_payload);
                    if(is_valid_rate(rate)) {
                        print_mag_rate = rate;
                    } else {
                        print_to_buff("$ERR,1*", &UART_output_buff);
                    }
                }
            }
            UART_input_buff.read = (UART_input_buff.read + 1) % INPUT_BUFF_LEN;
        }
        tmr_wait_period(TIMER1);
    }
    return 0;
}

void __attribute__((__interrupt__, no_auto_psv)) _U1TXInterrupt(void){
    IFS0bits.U1TXIF = 0; // clear TX interrupt flag


    if(UART_output_buff.read == UART_output_buff.write){
        UART_INTERRUPT_TX_MANUAL_TRIG = 1;
    } 

    while(!U1STAbits.UTXBF && UART_output_buff.read != UART_output_buff.write){
        U1TXREG = UART_output_buff.buff[UART_output_buff.read];
        UART_output_buff.read = (UART_output_buff.read + 1) % OUTPUT_BUFF_LEN;
    }
}

void __attribute__((__interrupt__, no_auto_psv)) _U1RXInterrupt(void) {
    IFS0bits.U1RXIF = 0; //resetting the interrupt flag to 0

    while(U1STAbits.URXDA) {
        const char read_char = U1RXREG;

        const int new_write_index = (UART_input_buff.write + 1) % INPUT_BUFF_LEN;
        if (new_write_index != UART_input_buff.read) {
            UART_input_buff.buff[UART_input_buff.write] = read_char;
            UART_input_buff.write = new_write_index;
        }
    }
}
