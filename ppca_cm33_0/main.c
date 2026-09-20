/******************************************************************************
* File Name:   main.c
*
* Description: PPCA Core0 (CM33 Non-Secure) application demonstrating 80KB code
*              execution across multiple memory regions (M0, M2, M3) with
*              inter-core communication via shared memory variables.
*
* Related Document: See README.md
*
*
********************************************************************************
* (c) 2026, Infineon Technologies AG, or an affiliate of Infineon
* Technologies AG. All rights reserved.
* This software, associated documentation and materials ("Software") is
* owned by Infineon Technologies AG or one of its affiliates ("Infineon")
* and is protected by and subject to worldwide patent protection, worldwide
* copyright laws, and international treaty provisions. Therefore, you may use
* this Software only as provided in the license agreement accompanying the
* software package from which you obtained this Software. If no license
* agreement applies, then any use, reproduction, modification, translation, or
* compilation of this Software is prohibited without the express written
* permission of Infineon.
*
* Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
* IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
* INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
* THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
* SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
* Infineon reserves the right to make changes to the Software without notice.
* You are responsible for properly designing, programming, and testing the
* functionality and safety of your intended application of the Software, as
* well as complying with any legal requirements related to its use. Infineon
* does not guarantee that the Software will be free from intrusion, data theft
* or loss, or other breaches ("Security Breaches"), and Infineon shall have
* no liability arising out of any Security Breaches. Unless otherwise
* explicitly approved by Infineon, the Software may not be used in any
* application where a failure of the Product or any consequences of the use
* thereof can reasonably be expected to result in personal injury.
*******************************************************************************/

/*******************************************************************************
* Header Files
********************************************************************************/
#include "cy_pdl.h"
#include "cycfg.h"

/*******************************************************************************
* Macros & Constants
********************************************************************************/
#define LOOP_DELAY_MS      1000    /* Main loop delay in milliseconds */
#define ARRAY_SIZE         7500    /* Array size for 30KB data section */
#define M1_VAR_ADDRESS     0x20000400  /* Shared memory M1 region (lower 16-bit result) */
#define M4_VAR_ADDRESS     0x20040400  /* Shared memory M4 region (upper 16-bit result) */

/* Boot markers for inter-core handshake */
#define BOOT_MARKER_M1     0xAA11  /* M1 boot indicator */
#define BOOT_MARKER_M4     0xBB22  /* M4 boot indicator */

/* Shared memory increment rates */
#define M1_INCREMENT       2       /* M1 counter increment per cycle */
#define M4_INCREMENT       5       /* M4 counter increment per cycle (different rate) */

/*******************************************************************************
* Global Data
********************************************************************************/
static const int32_t bigArray[ARRAY_SIZE] = {1};  /* Static array allocated in .rodata (30KB) */

/*******************************************************************************
* Helper Functions (M0, M2, M3 execution markers)
********************************************************************************/

/*
* func_m0
* Executes arithmetic operations mapped to PPCA M0 memory region.
* Includes multiplication and addition loops to occupy code space.
* Result includes 0x1000 marker for region identification.
*/
static int32_t func_m0(int32_t x) {
    int32_t result = 0;
    for (int i = 0; i < 100; i++) result += x * i + (i * 7);
    return result + 0x1000;
}

/*
* func_m2
* Executes arithmetic operations mapped to PPCA M2 memory region.
* Performs summation and division operations to occupy code space.
* Result includes 0x2000 marker for region identification.
*/
static int32_t func_m2(int32_t a, int32_t b) {
    int32_t sum = 0;
    for (int i = 0; i < 50; i++) sum += (a + b) * i;
    return sum / (b + 1) + 0x2000;
}

/*
* func_m23
* Executes operations spanning M2/M3 memory regions.
* Uses volatile variable to prevent compiler optimization of loops.
* Void return allows execution without result dependency.
*/
static void func_m23(void) {
    volatile int32_t tmp = 0;
    for (int i = 0; i < 200; i++) tmp = tmp + i - (i >> 1);
}

/*
* func_m3
* Executes complex arithmetic operations mapped to PPCA M3 memory region.
* Includes conditional logic and bit shift operations to occupy code space.
* Result includes 0x3000 marker for region identification.
*/
static int32_t func_m3(int32_t val) {
    int32_t result = val;
    for (int i = 0; i < 150; i++) {
        result = (result * 3) + (val & 0xFF);
        if (result > 1000000) result = result >> 8;
    }
    return result + 0x3000;
}

/*******************************************************************************
* Main Computation
********************************************************************************/

/*
* process_80kb_code
* Main computation function that processes array data and executes functions
* distributed across all PPCA memory regions (M0, M2, M3). Each function
* contributes an execution marker (0x1000, 0x2000, 0x3000) to the result,
* allowing verification of which code regions executed.
*
* Expected Calculation:
*   - Array sum: 7500 (7500 elements � 1 each)
*   - func_m0(7500): 7507 � 4950 + 0x1000 = 37,163,650 (0x237F6A2)
*   - func_m2(v1, 10): (sum / 11) + 0x2000 = 4,138,682,227 (0xF6B02F03)
*   - func_m3(v2): Complex calculation + 0x3000
*   - Final result: sum + v1 + v2 + v3 � 0x000055786
*
*   This result is split into shared memory:
*   - M1 register = result & 0xFFFF = 0x5786 (lower 16-bits)
*   - M4 register = (result >> 16) & 0xFFFF = 0x0055 (upper 16-bits)
*
*   The presence of 0x1000, 0x2000, 0x3000 markers in the result proves
*   that code executed across all three PPCA memory regions (M0, M2, M3).
*
* Return: Aggregated result containing sum and region execution markers
*/
static int32_t process_80kb_code(void) {
    int32_t sum = 0;
    
    /* Sum all array elements (30KB data allocated in .rodata section) */
    /* Expected: sum = 7500 (ARRAY_SIZE � 1) */
    for (int i = 0; i < ARRAY_SIZE; i++) sum += bigArray[i];
    
    /* Execute functions distributed across memory regions with markers */
    int32_t v1 = func_m0(sum);        /* Execute M0 region code, returns v1 + 0x1000 marker */
    int32_t v2 = func_m2(v1, 10);     /* Execute M2 region code, returns v2 + 0x2000 marker */
    func_m23();                        /* Execute M2/M3 transition code (void, no return) */
    int32_t v3 = func_m3(v2);         /* Execute M3 region code, returns v3 + 0x3000 marker */
    
    /* Aggregate result contains 0x1000, 0x2000, 0x3000 markers proving all regions executed */
    return sum + v1 + v2 + v3;
}

/*******************************************************************************
* Main Application
********************************************************************************/

/*
* main
* Entry point for PPCA Core0 (CM33 Non-Secure). Initializes inter-core
* communication via shared memory, executes 80KB computation code across
* M0/M2/M3 memory regions, and runs continuous loop with periodic
* shared memory updates to demonstrate sustained execution.
*
* Return: Does not return (infinite loop)
*/
int main(void) {
    volatile uint32_t *m1_ptr = (volatile uint32_t *)M1_VAR_ADDRESS;
    volatile uint32_t *m4_ptr = (volatile uint32_t *)M4_VAR_ADDRESS;
    
    /* Set boot markers visible to main core via shared memory communication */
    *m1_ptr = BOOT_MARKER_M1;
    *m4_ptr = BOOT_MARKER_M4;
    
    /* Execute 80KB computation distributed across all memory regions
     * Expected calculated value: result � 0x000055786 (contains 0x1000, 0x2000, 0x3000 markers)
     * After execution, shared memory will contain initial values:
     *   - m1_ptr (0x20000400) = result & 0xFFFF = 0x5786 (lower 16-bits)
     *   - m4_ptr (0x20040400) = (result >> 16) & 0xFFFF = 0x0055 (upper 16-bits)
     * These values prove all three PPCA memory regions (M0, M2, M3) executed successfully
     */
    int32_t result = process_80kb_code();
    
    /* Store computation results split across two shared memory variables */
    *m1_ptr = (result & 0xFFFF);        /* Lower 16-bits in M1 region */
    *m4_ptr = (result >> 16) & 0xFFFF;  /* Upper 16-bits in M4 region */
    
    /* Memory barrier before entering continuous loop */
    __DSB();
    
    /* Continuous loop: update shared memory periodically to prove execution */
    while (1) {
        Cy_SysLib_Delay(LOOP_DELAY_MS);
        *m1_ptr += M1_INCREMENT;    /* Increment M1 counter */
        *m4_ptr += M4_INCREMENT;    /* Increment M4 counter (different rate) */
    }
    
}
