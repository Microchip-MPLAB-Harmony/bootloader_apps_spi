/*******************************************************************************
  SPI PLIB

  Company:
    Microchip Technology Inc.

  File Name:
    plib_spi3_slave.c

  Summary:
    SPI3 Slave Source File

  Description:
    This file has implementation of all the interfaces provided for particular
    SPI peripheral.

*******************************************************************************/

/*******************************************************************************
* Copyright (C) 2019-2020 Microchip Technology Inc. and its subsidiaries.
*
* Subject to your compliance with these terms, you may use Microchip software
* and any derivatives exclusively with Microchip products. It is your
* responsibility to comply with third party license terms applicable to your
* use of third party software (including open source software) that may
* accompany Microchip software.
*
* THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER
* EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY IMPLIED
* WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS FOR A
* PARTICULAR PURPOSE.
*
* IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE,
* INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND
* WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS
* BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO THE
* FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL CLAIMS IN
* ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT OF FEES, IF ANY,
* THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS SOFTWARE.
*******************************************************************************/
#include "plib_spi3_slave.h"
#include "peripheral/gpio/plib_gpio.h"
#include <string.h>
#include "interrupts.h"
// *****************************************************************************
// *****************************************************************************
// Section: SPI3 Slave Implementation
// *****************************************************************************
// *****************************************************************************
#define SPI3_BUSY_PIN                    GPIO_PIN_RC14
#define SPI3_CS_PIN                      GPIO_PIN_RC11


#define SPI3_READ_BUFFER_SIZE            2080
#define SPI3_WRITE_BUFFER_SIZE           256

volatile static uint8_t SPI3_ReadBuffer[SPI3_READ_BUFFER_SIZE];
volatile static uint8_t SPI3_WriteBuffer[SPI3_WRITE_BUFFER_SIZE];


/* Global object to save SPI Exchange related data */
volatile static SPI_SLAVE_OBJECT spi3Obj;

#define SPI3_CON_CKP                        (0UL << _SPI3CON_CKP_POSITION)
#define SPI3_CON_CKE                        (1UL << _SPI3CON_CKE_POSITION)
#define SPI3_CON_MODE_32_MODE_16            (0UL << _SPI3CON_MODE16_POSITION)
#define SPI3_CON_ENHBUF                     (1UL << _SPI3CON_ENHBUF_POSITION)
#define SPI3_CON_STXISEL                    (3UL << _SPI3CON_STXISEL_POSITION)
#define SPI3_CON_SRXISEL                    (1UL << _SPI3CON_SRXISEL_POSITION)
#define SPI3_CON_SSEN                       (1UL << _SPI3CON_SSEN_POSITION)

#define SPI3_ENABLE_RX_INT()                IEC1SET = 0x20000
#define SPI3_CLEAR_RX_INT_FLAG()            IFS1CLR = 0x20000

#define SPI3_DISABLE_TX_INT()               IEC1CLR = 0x10000
#define SPI3_ENABLE_TX_INT()                IEC1SET = 0x10000
#define SPI3_CLEAR_TX_INT_FLAG()            IFS1CLR = 0x10000

#define SPI3_ENABLE_ERR_INT()               IEC1SET = 0x8000
#define SPI3_CLEAR_ERR_INT_FLAG()           IFS1CLR = 0x8000

/* Forward declarations */
static void SPI3_CS_Handler(GPIO_PIN pin, uintptr_t context);

static void mem_copy(volatile void* pDst, volatile void* pSrc, uint32_t nBytes)
{
    volatile uint8_t* pSource = (volatile uint8_t*)pSrc;
    volatile uint8_t* pDest = (volatile uint8_t*)pDst;

    for (uint32_t i = 0U; i < nBytes; i++)
    {
        pDest[i] = pSource[i];
    }
}

void SPI3_Initialize ( void )
{
    /* Disable SPI3 Interrupts */
    IEC1CLR = 0x8000;
    IEC1CLR = 0x20000;
    IEC1CLR = 0x10000;

    /* STOP and Reset the SPI */
    SPI3CON = 0;

    /* Clear SPI3 Interrupt flags */
    IFS1CLR = 0x8000;
    IFS1CLR = 0x20000;
    IFS1CLR = 0x10000;

    /* CLear the receiver overflow error flag */
    SPI3STATCLR = _SPI3STAT_SPIROV_MASK;

    /*
    SRXISEL = 1 (Receive buffer is not empty)
    STXISEL = 3 (Transmit buffer is not full)
    MSTEN = 0
    CKP = 0
    CKE = 1
    MODE< 32,16 > = 0
    ENHBUF = 1
    */

    SPI3CONSET = (SPI3_CON_ENHBUF | SPI3_CON_MODE_32_MODE_16 | SPI3_CON_CKE | SPI3_CON_CKP | SPI3_CON_SSEN | SPI3_CON_STXISEL | SPI3_CON_SRXISEL);

    /* Enable generation of interrupt on receiver overflow */
    SPI3CON2SET = _SPI3CON2_SPIROVEN_MASK;

    spi3Obj.rdInIndex = 0;
    spi3Obj.wrOutIndex = 0;
    spi3Obj.nWrBytes = 0;
    spi3Obj.errorStatus = SPI_SLAVE_ERROR_NONE;
    spi3Obj.callback = NULL ;
    spi3Obj.transferIsBusy = false ;
    spi3Obj.csInterruptPending = false;
    spi3Obj.rxInterruptActive = false;

    /* Set the Busy Pin to ready state */
    GPIO_PinWrite((GPIO_PIN)SPI3_BUSY_PIN, 0);

    /* Register callback and enable notifications on Chip Select logic level change */
    (void)GPIO_PinInterruptCallbackRegister(SPI3_CS_PIN, SPI3_CS_Handler, 0U);
    GPIO_PinInterruptEnable(SPI3_CS_PIN);

    /* Enable SPI3 RX and Error Interrupts. TX interrupt will be enabled when a SPI write is submitted. */
    SPI3_ENABLE_RX_INT();
    SPI3_ENABLE_ERR_INT();

    /* Enable SPI3 */
    SPI3CONSET = _SPI3CON_ON_MASK;
}

/* For 16-bit/32-bit mode, the "size" must be specified in terms of 16-bit/32-bit words */
size_t SPI3_Read(void* pRdBuffer, size_t size)
{
    size_t rdSize = size;
    uint32_t rdInIndex = spi3Obj.rdInIndex;

    if (rdSize > rdInIndex)
    {
        rdSize = rdInIndex;
    }

   (void) mem_copy(pRdBuffer, SPI3_ReadBuffer, rdSize);

    return rdSize;
}

/* For 16-bit/32-bit mode, the "size" must be specified in terms of 16-bit/32-bit words */
size_t SPI3_Write(void* pWrBuffer, size_t size )
{
    size_t wrSize = size;
    size_t wrOutIndex = 0;

    SPI3_DISABLE_TX_INT();

    if (wrSize > SPI3_WRITE_BUFFER_SIZE)
    {
        wrSize = SPI3_WRITE_BUFFER_SIZE;
    }

    (void) mem_copy(SPI3_WriteBuffer, pWrBuffer, wrSize);

    spi3Obj.nWrBytes = wrSize;

    /* Fill up the FIFO as long as there are empty elements */
    while ((!(SPI3STAT & _SPI3STAT_SPITBF_MASK)) && (wrOutIndex < wrSize))
    {
        SPI3BUF = SPI3_WriteBuffer[wrOutIndex];
        wrOutIndex++;
    }

    spi3Obj.wrOutIndex = wrOutIndex;

    /* Enable TX interrupt */
    SPI3_ENABLE_TX_INT();

    return wrSize;
}

/* For 16-bit/32-bit mode, the return value is in terms of 16-bit/32-bit words */
size_t SPI3_ReadCountGet(void)
{
    return spi3Obj.rdInIndex;
}

/* For 16-bit/32-bit mode, the return value is in terms of 16-bit/32-bit words */
size_t SPI3_ReadBufferSizeGet(void)
{
    return SPI3_READ_BUFFER_SIZE;
}

/* For 16-bit/32-bit mode, the return value is in terms of 16-bit/32-bit words */
size_t SPI3_WriteBufferSizeGet(void)
{
    return SPI3_WRITE_BUFFER_SIZE;
}

void SPI3_CallbackRegister(SPI_SLAVE_CALLBACK callBack, uintptr_t context )
{
    spi3Obj.callback = callBack;

    spi3Obj.context = context;
}

/* The status is returned as busy when CS is asserted */
bool SPI3_IsBusy(void)
{
    return spi3Obj.transferIsBusy;
}

/* Drive the GPIO pin to indicate to SPI Master that the slave is ready now */
void SPI3_Ready(void)
{
    GPIO_PinWrite((GPIO_PIN)SPI3_BUSY_PIN, 0);
}

SPI_SLAVE_ERROR SPI3_ErrorGet(void)
{
    SPI_SLAVE_ERROR errorStatus = spi3Obj.errorStatus;

    spi3Obj.errorStatus = SPI_SLAVE_ERROR_NONE;

    return errorStatus;
}

static void __attribute__((used)) SPI3_CS_Handler(GPIO_PIN pin, uintptr_t context)
{
    bool activeState = false;

    if (GPIO_PinRead((GPIO_PIN)SPI3_CS_PIN) == activeState)
    {
        /* CS is asserted */
        spi3Obj.transferIsBusy = true;

        /* Drive busy line to active state */
        GPIO_PinWrite((GPIO_PIN)SPI3_BUSY_PIN, 1);
    }
    else
    {
        /* Give application callback only if RX interrupt is not preempted and RX interrupt is not pending to be serviced */

        bool rxInterruptActive = spi3Obj.rxInterruptActive;

        if (((IFS1 & _IFS1_SPI3RXIF_MASK) == 0) && (rxInterruptActive == false))
        {
            /* CS is de-asserted */
            spi3Obj.transferIsBusy = false;

            spi3Obj.wrOutIndex = 0;
            spi3Obj.nWrBytes = 0;

            if(spi3Obj.callback != NULL)
            {
                uintptr_t context_val = spi3Obj.context;

                spi3Obj.callback(context_val);
            }

            /* Clear the read index. Application must read out the data by calling SPI3_Read API in the callback */
            spi3Obj.rdInIndex = 0;
        }
        else
        {
            /* If CS interrupt is serviced by either preempting the RX interrupt or RX interrupt is pending to be serviced,
             * then delegate the responsibility of giving application callback to the RX interrupt handler */

            spi3Obj.csInterruptPending = true;
        }
    }
}

void __attribute__((used)) SPI3_ERR_InterruptHandler (void)
{
    spi3Obj.errorStatus =(SPI3STAT & _SPI3STAT_SPIROV_MASK);

    /* Clear the receive overflow flag */
    SPI3STATCLR = _SPI3STAT_SPIROV_MASK;

    SPI3_CLEAR_ERR_INT_FLAG();
}

void __attribute__((used)) SPI3_TX_InterruptHandler (void)
{
    size_t wrOutIndex = spi3Obj.wrOutIndex;
    size_t nWrBytes = spi3Obj.nWrBytes;

    /* Fill up the FIFO as long as there are empty elements */
    while ((!(SPI3STAT & _SPI3STAT_SPITBF_MASK)) && (wrOutIndex < nWrBytes))
    {
        SPI3BUF = SPI3_WriteBuffer[wrOutIndex];
        wrOutIndex++;
    }

    spi3Obj.wrOutIndex = wrOutIndex;

    /* Clear the transmit interrupt flag */
    SPI3_CLEAR_TX_INT_FLAG();

    if (spi3Obj.wrOutIndex == nWrBytes)
    {
        /* Nothing to transmit. Disable transmit interrupt. The last byte sent by the master will be shifted out automatically*/
        SPI3_DISABLE_TX_INT();
    }
}

void __attribute__((used)) SPI3_RX_InterruptHandler (void)
{
    uint32_t receivedData = 0;

    spi3Obj.rxInterruptActive = true;

    size_t rdInIndex = spi3Obj.rdInIndex;

    while (!(SPI3STAT & _SPI3STAT_SPIRBE_MASK))
    {
        /* Receive buffer is not empty. Read the received data. */
        receivedData = SPI3BUF;

        if (rdInIndex < SPI3_READ_BUFFER_SIZE)
        {
            SPI3_ReadBuffer[rdInIndex] = (uint8_t)receivedData;
            rdInIndex++;
        }
    }

    spi3Obj.rdInIndex = rdInIndex;

    /* Clear the receive interrupt flag */
    SPI3_CLEAR_RX_INT_FLAG();

    spi3Obj.rxInterruptActive = false;

    /* Check if CS interrupt occured before the RX interrupt and that CS interrupt delegated the responsibility to give
     * application callback to the RX interrupt */

    if (spi3Obj.csInterruptPending == true)
    {
        spi3Obj.csInterruptPending = false;
        spi3Obj.transferIsBusy = false;

        spi3Obj.wrOutIndex = 0;
        spi3Obj.nWrBytes = 0;

        if(spi3Obj.callback != NULL)
        {
            uintptr_t context = spi3Obj.context;

            spi3Obj.callback(context);
        }

        /* Clear the read index. Application must read out the data by calling SPI3_Read API in the callback */
        spi3Obj.rdInIndex = 0;
    }
}

