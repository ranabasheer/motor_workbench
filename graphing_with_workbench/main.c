

/**
 * main.c
 */
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <stddef.h>
#include "DSP28x_project.h"
#include "F2806X_EPwm.h"
#include "serial_log.h"
#include <serial_log_interface.h>


typedef struct coil_currents_t {
    float a;
    float b;
    float c;
} coil_currents_t;

typedef struct clarke_currents_t {
    float alpha;
    float beta;
} clarke_currents_t;

typedef struct park_currents_t {
    float direct;
    float quadrature;
} park_currents_t;

#define radians(angle) ((angle)*3.14/180.0)

//heap variables
uint32_t log_memory[3000];
int amplitude;
int frequency;

/*
 * Callback function when the knob for frequency is adjusted
 */
void freq_update(int freq)
{
    frequency = freq;
}

/*
 * Callback function when the knob for voltage is adjusted
 */
void volt_update(int volt)
{
    amplitude = volt;
}

/*
 * Generates a three phase sinusoidal current based on the input amplitude
 * which varies from 0 to 100 and the angle that is in degrees
 */
coil_currents_t generate_sinusoidal_currents(float amplitude, float angle)
{
    coil_currents_t current;
    current.a = amplitude*sinf(radians(angle));
    current.b = amplitude*sinf(radians(angle + 120.0));
    current.c = amplitude*sinf(radians(angle - 120.0));
    return current;
}

/*
 * applies clarke transformation on 3-phase sinusoidal currents
 */

clarke_currents_t clarke_transformation(volatile coil_currents_t *current)
{
    clarke_currents_t clarke;
    //perform clarke transformation
    clarke.alpha = 2*(current->a - 0.5*current->b - 0.5*current->c)/3.0;
    clarke.beta = (current->b - current->c)/1.7320508075688772;
    return clarke;
}

/*
 * Translate the 2-phase clarke currents to the frame of reference of the rotor
 * by applying the park transformation
 */
park_currents_t park_transformation(volatile clarke_currents_t *clarke, float angle)
{
    park_currents_t park;
    angle = -radians(angle); //negative value is because you are trying to negate
                             //the rotation of the motor to transform the time varying
                            //clarke currents to a frame of reference of the rotor
    park.direct = cosf(angle)*clarke->alpha + sinf(angle)*clarke->beta;
    park.quadrature = -sinf(angle)*clarke->alpha + cosf(angle)*clarke->beta;
    return park;
}

__interrupt void commutation_timer_isr(void)
{
    serial_log_sample_data();
}

/*
 * CPU Timer1 is used to set the commutation frequency. Timer 1
 * interrupt is generated every 926us which increments the electrical angle of
 * commutation by comm_speed/10 degrees. comm_speed is a variable that is set by
 * the webUI which can vary from 0 to 100 which corresponds to commutation frequency
 * of 0 to 30 Hz respectively. At 30 Hz, comm_speed value is 100 resulting in electrical
 * angle of the motor incrementing by 10 degrees every interrupt. So to complete
 * one full electrical rotation of 360 degrees, it takes 36 x 926us =  33.28 milliseconds
 * resulting in the 30 Hz commutation frequency
 */
void init_commutation_timer()
{
    //
    // Interrupts that are used in this example are re-mapped to
    // ISR functions found within this file.
    //
    EALLOW;  // This is needed to write to EALLOW protected registers
    PieVectTable.TINT1 = &commutation_timer_isr;
    EDIS;    // This is needed to disable write to EALLOW protected registers

    //inputs to this function takes the CPU frequency of 90MHz and the desired timer1
    //interrupt period of 926 microseconds
    ConfigCpuTimer(&CpuTimer1, 90, 926);

    CpuTimer1Regs.TCR.bit.TSS = 0; //start the timer

    IER |= M_INT13; //Enable Timer1 Core interrupt
}

int main(void)
{
    uint32_t prev_current_time = 0;
    float rotor_position = 0.0;
    //void *log_clarke, *log_park, *log_currents;
    volatile coil_currents_t current;
    volatile clarke_currents_t clarke;
    volatile park_currents_t park;
    //volatile float current_a, current_b, current_c;
    //volatile float alpha, beta;
    //volatile float direct, quadrature;

    InitSysCtrl();
    InitSciGpio();
    InitCpuTimers();

    serial_log_init(log_memory, sizeof(log_memory), 1000);
    if(serial_log_output("Coil Currents", 300, 3, "A", &current.a, "B", &current.b, "C", &current.c) == NULL)
    {
        return 1;
    }

    /*log_clarke = serial_log_output("Clarke Transformation", 300, 2, "alpha", &clarke.alpha, "beta", &clarke.beta);
    if(log_clarke == NULL)
    {
        return 1;
    }

    log_park = serial_log_output("Park Transformation", 300, 2, "direct", &park.direct, "quadrature", &park.quadrature);
    if(log_park == NULL)
    {
        return 1;
    }*/

    if(serial_log_input("Frequency", 10, freq_update) == NULL)
    {
        return 1;
    }

    if(serial_log_input("Voltage", 10, volt_update) == NULL)
    {
        return 1;
    }

    prev_current_time = 0;

    serial_log_init_time();
    init_commutation_timer();
    //Enable global interrupts
    EnableInterrupts();
    current.a = 0;
    current.b = 0;
    current.c = 0;
    while(1)
    {
        uint32_t current_time = serial_log_get_time_ms();
        serial_log_handler(current_time);
        if(current_time == prev_current_time)
            continue;
        prev_current_time = current_time;


        rotor_position += frequency;

        current.a += 1.0;
        //current.b += 1.0;
        //current.c += 1.0;
        if(current.a > 300.0)
        {
            current.a = 0.0;
        }
        /*if(current.b > 500)
        {
            current.b = 0;
        }
        if(current.c > 400)
        {
            current.c = 0;
        }*/
        current.b = current.a;
        current.c = current.a;// + 10.0;
        //current = generate_sinusoidal_currents(amplitude, rotor_position);
        //clarke = clarke_transformation(&current);
        //park = park_transformation(&clarke, rotor_position);

        if(rotor_position > 360.0)
        {
            rotor_position = 0;
        }
    }

    return 0;
}
