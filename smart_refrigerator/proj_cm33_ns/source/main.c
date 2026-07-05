/******************************************************************************
* File Name:   main.c
*
* Description: Merged CM33 NS main: Wi-Fi Secure TCP + CM55 ML Deploy
*
*******************************************************************************/

/* Header file includes */
#include "cybsp.h"
#include "retarget_io_init.h"
#include "cycfg_peripherals.h"

/* FreeRTOS header file */
#include <FreeRTOS.h>
#include <task.h>
#include "cyabs_rtos.h"
#include "cyabs_rtos_impl.h"
#include "cy_time.h"

/* Secure TCP client task header file. */
#include "secure_tcp_client.h"

/* Macros */
#define TCP_SECURE_CLIENT_TASK_STACK_SIZE  (5U * 1024U)
#define TCP_SECURE_CLIENT_TASK_PRIORITY    (1U)

/* CM55 boot configuration */
#define CM55_BOOT_WAIT_TIME_USEC           (10U)
#define CM55_APP_BOOT_ADDR                 (CYMEM_CM33_0_m55_nvm_START + CYBSP_MCUBOOT_HEADER_SIZE)

/* Tickless idle timer configuration */
#define LPTIMER_0_WAIT_TIME_USEC           (62U)
#define APP_LPTIMER_INTERRUPT_PRIORITY     (1U)

/* Global Variables */
static mtb_hal_lptimer_t lptimer_obj;
static mtb_hal_rtc_t     rtc_obj;

/* Function Prototypes */
static void setup_clib_support(void);
static void lptimer_interrupt_handler(void);
static void setup_tickless_idle_timer(void);

/* handle_app_error() is defined in secure_tcp_client.c */

/*******************************************************************************
* Function Name: setup_clib_support
********************************************************************************
* Summary:
*    1. Configures and initializes the Real-Time Clock (RTC).
*    2. Initializes the RTC HAL object to enable CLIB support library.
*
*******************************************************************************/
static void setup_clib_support(void)
{
    /* RTC Initialization */
    Cy_RTC_Init(&CYBSP_RTC_config);
    Cy_RTC_SetDateAndTime(&CYBSP_RTC_config);

    /* Initialize the ModusToolbox CLIB support library */
    mtb_clib_support_init(&rtc_obj);
}

/*******************************************************************************
* Function Name: lptimer_interrupt_handler
********************************************************************************
* Summary:
* Interrupt handler function for LPTimer instance.
*
*******************************************************************************/
static void lptimer_interrupt_handler(void)
{
    mtb_hal_lptimer_process_interrupt(&lptimer_obj);
}

/*******************************************************************************
* Function Name: setup_tickless_idle_timer
********************************************************************************
* Summary:
* 1. Configures and initializes an interrupt for LPTimer.
* 2. Initializes the LPTimer HAL object for RTOS tickless idle mode.
* 3. Passes the LPTimer object to abstraction RTOS library.
*
*******************************************************************************/
static void setup_tickless_idle_timer(void)
{
    /* Interrupt configuration structure for LPTimer */
    cy_stc_sysint_t lptimer_intr_cfg =
    {
        .intrSrc      = CYBSP_CM33_LPTIMER_0_IRQ,
        .intrPriority = APP_LPTIMER_INTERRUPT_PRIORITY
    };

    /* Initialize the LPTimer interrupt */
    cy_en_sysint_status_t interrupt_init_status = 
        Cy_SysInt_Init(&lptimer_intr_cfg, lptimer_interrupt_handler);
    
    if(CY_SYSINT_SUCCESS != interrupt_init_status)
    {
        handle_app_error();
    }

    /* Enable NVIC interrupt */
    NVIC_EnableIRQ(lptimer_intr_cfg.intrSrc);

    /* Initialize the MCWDT block */
    cy_en_mcwdt_status_t mcwdt_init_status = 
        Cy_MCWDT_Init(CYBSP_CM33_LPTIMER_0_HW, &CYBSP_CM33_LPTIMER_0_config);

    if(CY_MCWDT_SUCCESS != mcwdt_init_status)
    {
        handle_app_error();
    }

    /* Enable MCWDT instance */
    Cy_MCWDT_Enable(CYBSP_CM33_LPTIMER_0_HW,
                    CY_MCWDT_CTR_Msk, 
                    LPTIMER_0_WAIT_TIME_USEC);

    /* Setup LPTimer using the HAL object */
    cy_rslt_t result = mtb_hal_lptimer_setup(&lptimer_obj, 
                                             &CYBSP_CM33_LPTIMER_0_hal_config);
    
    if(CY_RSLT_SUCCESS != result)
    {
        handle_app_error();
    }

    /* Pass the LPTimer object to abstraction RTOS library */
    cyabs_rtos_set_lptimer(&lptimer_obj);
}

/******************************************************************************
 * Function Name: main
 ******************************************************************************
 * Summary:
 *  1. Initializes BSP
 *  2. Sets up CLIB support (RTC)
 *  3. Initializes retarget-io for debug UART
 *  4. Sets up tickless idle timer
 *  5. Enables CM55 core for ML inference
 *  6. Creates Wi-Fi TCP secure client task
 *  7. Starts the RTOS scheduler
 *
 ******************************************************************************/
int main(void)
{
    cy_rslt_t result;

    /* Initialize the board support package */
    result = cybsp_init();
    if(CY_RSLT_SUCCESS != result)
    {
        handle_app_error();
    }

    /* Enable global interrupts */
    __enable_irq();

    /* Setup CLIB support library */
    setup_clib_support();

    /* Initialize retarget-io middleware */
    init_retarget_io();

    /* Setup the LPTimer instance for CM33 CPU */
    setup_tickless_idle_timer();

    /* Clear screen and print banner */
    printf("\x1b[2J\x1b[;H");
    printf("===============================================================\n");
    printf("PSOC Edge MCU: Wi-Fi + ML Deploy Merge\n");
    printf("===============================================================\n\n");

    /* Enable CM55 for ML inference */
    Cy_SysEnableCM55(MXCM55, CM55_APP_BOOT_ADDR, CM55_BOOT_WAIT_TIME_USEC);
    printf("CM55 enabled for ML inference\n\n");

    /* Create the Wi-Fi TCP secure client task */
    result = xTaskCreate(tcp_secure_client_task, "Network task",
                         TCP_SECURE_CLIENT_TASK_STACK_SIZE,
                         NULL, TCP_SECURE_CLIENT_TASK_PRIORITY, NULL);

    if(pdPASS == result)
    {
        /* Start the FreeRTOS scheduler */
        vTaskStartScheduler();
        
        /* Should never get here */
        handle_app_error();
    }
    else
    {
        handle_app_error();
    }
}