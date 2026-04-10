#include <stddef.h>                     
#include <stdbool.h>                    
#include <stdlib.h>                     
#include <stdio.h>
#include "definitions.h"                

// GLOBAL BUFFER & DMA FLAGS
#define LDR_BUFFER_SIZE 256
uint16_t lightBuffer[LDR_BUFFER_SIZE] = {0}; 
volatile bool dataReady = false; 

// DUAL-MODE & SECURITY STATE VARIABLES
typedef enum {
    MODE_NIGHTLIGHT,
    MODE_SECURITY
} SYSTEM_MODE;

volatile SYSTEM_MODE currentMode = MODE_NIGHTLIGHT;
volatile bool isLocked = true;

// Security Engine Variables
volatile int shadowCount = 0;
volatile bool shadowActive = false; 
volatile uint32_t timeoutClock = 0; 

// INTERRUPT CALLBACKS

// Switch Interrupt (SW1 on EIC Channel 2)
void EIC_Callback(uintptr_t context) {
    if (currentMode == MODE_NIGHTLIGHT) {
        currentMode = MODE_SECURITY;
        isLocked = true; 
        LED_Clear();  
        shadowCount = 0;
        timeoutClock = 0;    
        shadowActive = true; 
    } else {
        currentMode = MODE_NIGHTLIGHT;
        LED_Set(); 
    }
}

// DMA Callback
void DMAC_Callback(DMAC_TRANSFER_EVENT status, uintptr_t context) {
    if(status == DMAC_TRANSFER_EVENT_COMPLETE) {
        dataReady = true; 
    }
}

// MAIN FUNCTION
int main ( void )
{
    SYS_Initialize ( NULL );

    printf("\r\n==========================================\r\n");
    printf("ADVAITTEJ\r\n");
    printf("   ARCHITECTURE: DMA + EIC + PWM         \r\n");
    printf("==========================================\r\n");

    // Register Callbacks
    DMAC_ChannelCallbackRegister(DMAC_CHANNEL_0, DMAC_Callback, 0);
    EIC_CallbackRegister(EIC_PIN_2, EIC_Callback, 0); 

    LED_Set(); 
    ADC_Enable(); 
    TCC0_PWMStart();

    // Start initial DMA Transfer
    DMAC_ChannelTransfer(DMAC_CHANNEL_0, 
                         (const void *)&ADC_REGS->ADC_RESULT, 
                         lightBuffer, 
                         sizeof(lightBuffer));

    uint32_t sampleTimer = 0;

    // SUPER LOOP
    while ( true )
    {
        if (dataReady == true)
        {
            // Smoothing the Signal
            uint32_t sum = 0;
            for(int i = 0; i < LDR_BUFFER_SIZE; i++) {
                sum += lightBuffer[i];
            }
            uint16_t averageLight = sum / LDR_BUFFER_SIZE;

            // MODE 1: SMART DIMMER
            if (currentMode == MODE_NIGHTLIGHT) {
                
                int32_t mappedValue = ((int32_t)averageLight - 930) * 4095 / (4095 - 930);
                if (mappedValue < 0) mappedValue = 0;
                if (mappedValue > 4095) mappedValue = 4095;

                uint32_t dutyValue = ((uint32_t)mappedValue * (uint32_t)mappedValue) / 4095;
                TCC0_PWM24bitDutySet(TCC0_CHANNEL0, dutyValue); 
                
                // Convert 0-4095 to 0-100%
                uint32_t powerPercent = (dutyValue * 100) / 4095;

                printf("[MODE: DIMMER] LDR: %04u | Power: %3lu%%\r\n", averageLight, powerPercent);

            } 
            // MODE 2: SHADOW KEY SECURITY
            else {
                if (averageLight > 2500 && !shadowActive) { 
                    shadowCount++;
                    shadowActive = true;
                    timeoutClock = 0; 
                    printf("\r\n>>> PULSE %d DETECTED <<<\r\n", shadowCount);
                } 
                else if (averageLight < 1500) { 
                    shadowActive = false;
                }

                // Security Timeout Logic
                if (shadowCount > 0 && shadowCount < 3 && timeoutClock > 1500) {
                    shadowCount = 0;
                    timeoutClock = 0;
                    printf("\r\n!!! SECURITY TIMEOUT - ACCESS DENIED !!!\r\n");
                }

                if (shadowCount >= 3) {
                    isLocked = false;
                    TCC0_PWM24bitDutySet(TCC0_CHANNEL0, 4095); // Unlock!
                } else {
                    TCC0_PWM24bitDutySet(TCC0_CHANNEL0, 0); // Keep Locked
                }

                if (isLocked) {
                    printf("[MODE: SECURE] LDR: %04u | Pulses: %d | Status: LOCKED\r\n", averageLight, shadowCount);
                } else {
                    printf("[MODE: SECURE] LDR: %04u | Pulses: %d | Status: UNLOCKED!\r\n", averageLight, shadowCount);
                }
            }

            dataReady = false; 
            DMAC_ChannelTransfer(DMAC_CHANNEL_0, (const void *)&ADC_REGS->ADC_RESULT, lightBuffer, sizeof(lightBuffer)); 
        }

        // SOFTWARE CLOCK & TRIGGER
        sampleTimer++;
        if (sampleTimer >= 50000) {
            ADC_ConversionStart(); 
            sampleTimer = 0; 
            if (currentMode == MODE_SECURITY && isLocked) {
               timeoutClock++; 
            }
        }

        SYS_Tasks ( );
    }

    return ( EXIT_FAILURE );
}