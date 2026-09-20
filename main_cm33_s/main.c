/******************************************************************************
* File Name:   main.c
*
* Description: Main core (CM33 Secure) application that initializes
*              the PSOC™ Control C3M/P8 system, loads and executes an 80KB PPCA Core0 application
*              across multiple memory regions (M0, M2, M3), and monitors PPCA
*              execution via shared memory communication.
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
#include "cybsp.h"
#include "cy_retarget_io.h"

/*******************************************************************************
* Macros
********************************************************************************/

/*
 * PPCA Core0 image contiguous in flash at 0x12030000 (secure cached alias).
 * POSTBUILD places the full 80KB binary at S_SBUS 0x32030000:
 *   binary[0x0000..0x13FFF] ? 0x32030000..0x32043FFF
 *
 * Aliases:
 *   S_SBUS  0x32030000  |  C_S  0x12030000  |  C_NS  0x02030000
 *
 * Cy_System_Copy_PPCA_Image_To_Memory() uses DW0 channel 0.
 * The original code using 0x12030000 + this API was confirmed working.
 */
#define CORE0_IMAGE_ADDRESS   0x12030000UL

/* Word offset for 32KB image size calculations */
#define WORD_OFFSET           0x04

/* PPCA M0/M2 memory region size (32KB each) (32KB - 4 bytes for offset) */
#define IMAGE_SIZE_32K        (0x8000 - WORD_OFFSET)
/* PPCA M3 memory region size (16KB) (16KB - 4 bytes for offset) */
#define IMAGE_SIZE_16K        (0x4000 - WORD_OFFSET)
/* Total PPCA application size (80KB = 32K + 32K + 16K) (80KB - 4 bytes for offset)  */
#define IMAGE_SIZE_TOTAL      (0x14000 - WORD_OFFSET)

/* PPCA SRAM addresses (main core's secure view) for readback diagnostics */
#define PPCA_SRAM_M0_ADDR     (PPCA_BASE + 0x00010000UL)  /* 0x53010000 */
#define PPCA_SRAM_M2_ADDR     (PPCA_BASE + 0x00030000UL)  /* 0x53030000 */
#define PPCA_SRAM_M3_ADDR     (PPCA_BASE + 0x00040000UL)  /* 0x53040000 */

/* Shared memory addresses remapped to main core view */
/* PPCA M1 region mapped to 0x53020400 (lower 16-bit result from PPCA) */
#define PPCA_M1_VAR_ADDRESS   0x53020400
/* PPCA M4 region mapped to 0x53050400 (upper 16-bit result from PPCA) */
#define PPCA_M4_VAR_ADDRESS   0x53050400

/*******************************************************************************
* Global Variables
********************************************************************************/
/* Debug UART variables */
static cy_stc_scb_uart_context_t    DEBUG_UART_context; /* DEBUG_UART context */
static mtb_hal_uart_t               DEBUG_UART_hal_obj; /* Debug DEBUG_UART HAL object */

/*******************************************************************************
* Function Prototypes
********************************************************************************/


/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
* Entry point for the main core (CM33 Secure). Initializes system peripherals,
* loads 80KB PPCA Core0 application across M0/M2/M3 memory regions, enables
* PPCA execution, and continuously monitors shared memory for PPCA core updates.
*
* Parameters:
*  void
*
* Return:
*  int (does not return - infinite loop)
*
*******************************************************************************/

int main(void)
{
    cy_rslt_t result;
    /* Initialize the device and board peripherals */
    result = cybsp_init();

    /* Device initialization failed - stop execution */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Initialize UART peripheral for debug output */
    Cy_SCB_UART_Init(DEBUG_UART_HW, &DEBUG_UART_config, &DEBUG_UART_context);
    Cy_SCB_UART_Enable(DEBUG_UART_HW);

    /* Setup HAL layer for UART communication */
    result = mtb_hal_uart_setup(&DEBUG_UART_hal_obj, &DEBUG_UART_hal_config,
                                &DEBUG_UART_context, NULL);

    /* HAL UART initialization failed - stop execution */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Redirect standard I/O to UART for printf() support */
    result = cy_retarget_io_init(&DEBUG_UART_hal_obj);

    /* Retarget I/O initialization failed - stop execution */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
    }

    /* Clear terminal screen and print header */
    /* \x1b[2J\x1b[;H - ANSI ESC sequence to clear screen and home cursor */
    printf("\x1b[2J\x1b[;H");
    printf("************************************************************\r\n");
    printf("PSOC Control C3M/P8: Single PPCA core full SRAM application\r\n");
    printf("************************************************************\r\n\n");

    /* Initialize PPCA configuration and connect to IO */
    Cy_PPCA_CNFG_Init(PPCA_CNFG_HW, &PPCA_CNFG_config);
    Cy_PPCA_Enable(PPCA_CNFG_HW);

    /* Enable memory remap: PPCA CPU sees M0+M2+M3 as contiguous 80KB at 0x0.
     * The generated PPCA_CNFG_config has remapEnable=false; we must set it
     * for the 80KB full-SRAM linker model. Without remap the PPCA CPU's
     * code address space does not map correctly to the SRAM banks. */
    PPCA_CNFG_HW->CTRL |= PPCA_CNFG_CTRL_REMAP_MEM_Msk;

    /* Enable PPCA RAM access before copying application image */
    Cy_System_PPCA_RAM_Enable();

    /* Pre-clear shared memory variables so we can distinguish
     * "PPCA never wrote" from uninitialized SRAM garbage */
    *(volatile uint32_t *)PPCA_M1_VAR_ADDRESS = 0;
    *(volatile uint32_t *)PPCA_M4_VAR_ADDRESS = 0;
    __DSB();

    /* Enable system interrupts */
    __enable_irq();

    /* Prepare PPCA image copy with intermediate address pointers.
     * POSTBUILD places 80KB binary contiguously at 0x32030000 (S_SBUS).
     * Secure cached alias: 0x12030000. */
    uint8_t *image_addr_m0 = (uint8_t *)CORE0_IMAGE_ADDRESS;
    uint8_t *image_addr_m2 = image_addr_m0 + IMAGE_SIZE_32K;
    uint8_t *image_addr_m3 = image_addr_m2 + IMAGE_SIZE_32K;

    /* Diagnostic: CPU (secure) reads flash directly to verify data is present.
     * Expected: SP=0x20004000, Reset=0x000004B5 (thumb bit set on 0x4B4). */
    {
        volatile uint32_t *flash_vec = (volatile uint32_t *)CORE0_IMAGE_ADDRESS;
        printf("Flash readback [0x%08X]: word0=0x%08X, word1=0x%08X\r\n",
               (unsigned int)CORE0_IMAGE_ADDRESS,
               (unsigned int)flash_vec[0], (unsigned int)flash_vec[1]);
    }

    /* Load and copy 80KB PPCA Core0 application into three memory regions */
    printf("Copying 80KB PPCA Core0 image in chunks:\r\n");
    printf("  M0: 32KB (0x%08X)\r\n", (unsigned int)image_addr_m0);
    Cy_System_Copy_PPCA_Image_To_Memory((void*)image_addr_m0, IMAGE_SIZE_32K, CY_PPCA_MEMORY_0);
    printf("  M2: 32KB (0x%08X)\r\n", (unsigned int)image_addr_m2);
    Cy_System_Copy_PPCA_Image_To_Memory((void*)image_addr_m2, IMAGE_SIZE_32K, CY_PPCA_MEMORY_2);
    printf("  M3: 16KB (0x%08X)\r\n", (unsigned int)image_addr_m3);
    Cy_System_Copy_PPCA_Image_To_Memory((void*)image_addr_m3, IMAGE_SIZE_16K, CY_PPCA_MEMORY_3);

    __DSB();  /* Ensure all writes complete before booting PPCA */

    /* Diagnostic: read back first 2 words from M0 SRAM (vector table) */
    {
        volatile uint32_t *m0_vec = (volatile uint32_t *)PPCA_SRAM_M0_ADDR;
        printf("  Readback M0[0..1]: SP=0x%08X, Reset=0x%08X\r\n",
               (unsigned int)m0_vec[0], (unsigned int)m0_vec[1]);
    }

    printf("Total: 80KB copied successfully\r\n");

    /* Verify REMAP_MEM is set */
    printf("PPCA_CNFG CTRL = 0x%08X (bit0=REMAP, bit31=EN)\r\n",
           (unsigned int)PPCA_CNFG_HW->CTRL);

    /* Boot PPCA Core0 to start execution */
    Cy_SysEnable_PPCA_Core0();

    printf("PPCA Core0 boot initiated\r\n");
    
    /* Display PPCA memory address mapping from main core perspective */
    printf("\r\nAddress Map:\r\n");
    printf("PPCA M1 base: 0x20000000 (PPCA), Remapped : 0x53020000 (Main)\r\n");
    printf("PPCA M4 base: 0x20040000 (PPCA), Remapped : 0x53050000 (Main)\r\n\r\n");
    
    /* Wait for PPCA Core0 to initialize and start execution */
    printf("Waiting 2 seconds for PPCA core to initialize...\r\n");
    Cy_SysLib_Delay(2000);
    
    /* Create pointers to shared memory variables for monitoring */
    volatile uint32_t *ppca_core0_M1_var = (volatile uint32_t *)PPCA_M1_VAR_ADDRESS;
      volatile uint32_t *ppca_core0_M4_var = (volatile uint32_t *)PPCA_M4_VAR_ADDRESS;

    /* Display initial shared memory values */
    printf("Initial M1 value at 0x53020400: 0x%08X (expect computation result)\r\n", (unsigned int)*ppca_core0_M1_var);
    printf("Initial M4 value at 0x53050400: 0x%08X (expect computation result)\r\n\r\n", (unsigned int)*ppca_core0_M4_var);

    /* Track previous values to detect changes */
    volatile uint32_t prev0 = *ppca_core0_M1_var;
    volatile uint32_t prev1 = *ppca_core0_M4_var;

    /* Continuous loop: monitor shared memory and detect PPCA execution */
    for (;;)
    {
        /* Display current shared memory values */
        printf("\r\n Main Core: PPCA Core0 M1 Variable = (0x%08X)", (unsigned int)*ppca_core0_M1_var);
        printf("\r\n Main Core: PPCA Core0 M4 Variable = (0x%08X)", (unsigned int)*ppca_core0_M4_var);

        /* Memory barrier ensures all previous operations complete */
        __DSB();
         
        /* Toggle LED3 to indicate main core is running */
        Cy_GPIO_Inv(CYBSP_USER_LED3_PORT, CYBSP_USER_LED3_PIN);

        /* Detect M1 shared memory changes from PPCA Core0 */
        if(*ppca_core0_M1_var != prev0)
        {
            prev0 = *ppca_core0_M1_var;
            printf("\r\n*** M1 Value Changed! ***");
        }

        /* Detect M4 shared memory changes from PPCA Core0 */
        if(*ppca_core0_M4_var != prev1)
        {
            prev1 = *ppca_core0_M4_var;
            printf("\r\n*** M4 Value Changed! ***");
        }

        /* Delay before next monitoring cycle */
        Cy_SysLib_Delay(1000);
    }
}

