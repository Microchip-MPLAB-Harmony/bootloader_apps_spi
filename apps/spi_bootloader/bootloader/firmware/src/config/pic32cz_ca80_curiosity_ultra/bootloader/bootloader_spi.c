/*******************************************************************************
  SPI Bootloader Source File

  File Name:
    bootloader_spi.c

  Summary:
    This file contains source code necessary to execute SPI bootloader.

  Description:
    This file contains source code necessary to execute SPI bootloader.
    It implements bootloader protocol which uses SPI peripheral to download
    application firmware into internal flash.
 *******************************************************************************/

// DOM-IGNORE-BEGIN
/*******************************************************************************
* Copyright (C) 2019 Microchip Technology Inc. and its subsidiaries.
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
// DOM-IGNORE-END

// *****************************************************************************
// *****************************************************************************
// Section: Include Files
// *****************************************************************************
// *****************************************************************************

#include "definitions.h"
#include "bootloader_common.h"
#include <device.h>
#include <string.h>

// *****************************************************************************
// *****************************************************************************
// Section: Type Definitions
// *****************************************************************************
// *****************************************************************************

#define SET_BIT(reg, bits)                      ((reg) |= (bits))
#define CLR_BIT(reg, bits)                      ((reg) &= ~(bits))
#define IS_BIT_SET(reg, bit)                    (((reg) & (bit))? (true):(false))

#define BL_BUFFER_SIZE                          ((ERASE_BLOCK_SIZE) + (sizeof(uint32_t)))

/* MSB bit is set for the spi master to decide if a slave is present or not.
 * If slave is not present, the read value may be 0x00 or 0xFF.
 */
#define BL_STATUS_READY                         (0x80U)

#define BL_STATUS_BIT_BUSY                      ((uint8_t)0x01U << 0)
#define BL_STATUS_BIT_INVALID_COMMAND           ((uint8_t)0x01U << 1)
#define BL_STATUS_BIT_INVALID_MEM_ADDR          ((uint8_t)0x01U << 2)
#define BL_STATUS_BIT_COMMAND_EXECUTION_ERROR   ((uint8_t)0x01U << 3)      //Valid only when BL_STATUS_BIT_BUSY is 0
#define BL_STATUS_BIT_CRC_ERROR                 ((uint8_t)0x01U << 4)
#define BL_STATUS_BIT_COMM_ERROR                ((uint8_t)0x01U << 5)
#define BL_STATUS_BIT_ALL                       (BL_STATUS_BIT_BUSY | BL_STATUS_BIT_INVALID_COMMAND | BL_STATUS_BIT_INVALID_MEM_ADDR | \
                                                 BL_STATUS_BIT_COMMAND_EXECUTION_ERROR | BL_STATUS_BIT_CRC_ERROR | BL_STATUS_BIT_COMM_ERROR)


#define     BL_COMMAND_UNLOCK         0xA0U
#define     BL_COMMAND_ERASE          0xA1U
#define     BL_COMMAND_PROGRAM        0xA2U
#define     BL_COMMAND_VERIFY         0xA3U
#define     BL_COMMAND_RESET          0xA4U
#define     BL_COMMAND_READ_STATUS    0xA5U
#define     BL_COMMAND_READ_VERSION    0xA8U
#define     BL_COMMAND_MAX             0XA9U

typedef uint8_t BL_COMMAND;

typedef enum
{
    BL_FLASH_STATE_IDLE = 0,
    BL_FLASH_STATE_ERASE,
    BL_FLASH_STATE_WRITE,
    BL_FLASH_STATE_VERIFY,
    BL_FLASH_STATE_RESET,
    BL_FLASH_STATE_ERASE_BUSY_POLL,
    BL_FLASH_STATE_WRITE_BUSY_POLL,
}BL_FLASH_STATE;

typedef struct __PACKED
{
    uint8_t                                 cmd;
    uint32_t                                appImageStartAddr;
    uint32_t                                appImageSize;
}SPI_BL_CMD_UNLOCK;

typedef struct __PACKED
{
    uint8_t                                 cmd;
    uint32_t                                memAddr;
}SPI_BL_CMD_ERASE;

typedef struct __PACKED
{
    uint8_t                                 cmd;
    uint32_t                                nBytes;
    uint32_t                                memAddr;
    uint8_t                                 data[BL_BUFFER_SIZE];
}SPI_BL_CMD_PROGRAM;

typedef struct __PACKED
{
    uint8_t                                 cmd;
    uint32_t                                crc;
}SPI_BL_CMD_VERIFY;

typedef union __PACKED
{
    volatile uint8_t                        readBuffer[sizeof(SPI_BL_CMD_PROGRAM)];
    SPI_BL_CMD_UNLOCK                       unlockCommand;
    SPI_BL_CMD_ERASE                        eraseCommand;
    SPI_BL_CMD_PROGRAM                      programCommand;
    SPI_BL_CMD_VERIFY                       verifyCommand;
}SPI_BL_COMMAND_PROTOCOL;

typedef struct
{
    SPI_BL_COMMAND_PROTOCOL                 cmd;
    uint8_t                                 status;
    uint8_t                                 dataBufferAlignOffset;
    BL_FLASH_STATE                          flashState;
    uint32_t                                appImageStartAddr;
    uint32_t                                appImageEndAddr;
    uint32_t                                nFlashBytesWritten;
    volatile uint32_t                       nReadBytes;
    uint16_t                                btlVersion;
}SPI_BL_DATA;



// *****************************************************************************
// *****************************************************************************
// Section: Global objects
// *****************************************************************************
// *****************************************************************************

static SPI_BL_DATA                          spiBLData;

static bool spiBLInitDone   = false;

static bool spiBLActive     = false;

// *****************************************************************************
// *****************************************************************************
// Section: Bootloader Local Functions
// *****************************************************************************
// *****************************************************************************

static void BL_SPI_SubmitWriteRequest(void)
{
    /* NVM requires data buffer to align to 32-bit (word) boundary. Move the buffer content to align to 32-bit boundary. */
    if (((uint32_t)spiBLData.cmd.programCommand.data & 0x03U) != 0U)
    {
        spiBLData.dataBufferAlignOffset = (uint8_t)(4U - ((uint32_t)spiBLData.cmd.programCommand.data & 0x03U));

        (void) memmove(&spiBLData.cmd.programCommand.data[spiBLData.dataBufferAlignOffset], spiBLData.cmd.programCommand.data, spiBLData.cmd.programCommand.nBytes);
    }
    else
    {
        /* Already aligned to word boundary */
        spiBLData.dataBufferAlignOffset = 0;
    }

    SET_BIT(spiBLData.status, BL_STATUS_BIT_BUSY);
    spiBLData.nFlashBytesWritten = 0;
}

static void BL_SPI_CommandParser(void)
{
    if (spiBLActive == false)
    {
        if ((spiBLData.cmd.readBuffer[0] >= BL_COMMAND_UNLOCK) &&
            (spiBLData.cmd.readBuffer[0] < BL_COMMAND_MAX))
        {
            spiBLActive = true;
        }
    }

    switch(spiBLData.cmd.readBuffer[0])
    {
        case BL_COMMAND_UNLOCK:
            /* Make sure start address + size does not exceed the maximum flash address */
            if ((spiBLData.cmd.unlockCommand.appImageStartAddr + spiBLData.cmd.unlockCommand.appImageSize) > (FLASH_START + FLASH_LENGTH))
            {
                SET_BIT(spiBLData.status, BL_STATUS_BIT_INVALID_MEM_ADDR);
            }
            else
            {
                /* Save application start address and size for future reference */
                spiBLData.appImageStartAddr = spiBLData.cmd.unlockCommand.appImageStartAddr;
                spiBLData.appImageEndAddr = spiBLData.cmd.unlockCommand.appImageStartAddr + spiBLData.cmd.unlockCommand.appImageSize;
            }
            break;

        case BL_COMMAND_ERASE:

            if ((spiBLData.cmd.eraseCommand.memAddr >= spiBLData.appImageStartAddr) && ((spiBLData.cmd.eraseCommand.memAddr + ERASE_BLOCK_SIZE) <=  spiBLData.appImageEndAddr))
            {
                SET_BIT(spiBLData.status, BL_STATUS_BIT_BUSY);
                spiBLData.flashState = BL_FLASH_STATE_ERASE;
            }
            else
            {
                SET_BIT(spiBLData.status, BL_STATUS_BIT_INVALID_MEM_ADDR);
            }
            break;

        case BL_COMMAND_PROGRAM:

            if ((spiBLData.cmd.programCommand.memAddr < spiBLData.appImageStartAddr) || (spiBLData.cmd.programCommand.nBytes > BL_BUFFER_SIZE)
                     || ((spiBLData.cmd.programCommand.memAddr + spiBLData.cmd.programCommand.nBytes) > spiBLData.appImageEndAddr))
            {
                SET_BIT(spiBLData.status, BL_STATUS_BIT_INVALID_MEM_ADDR);
            }
            else
            {
                BL_SPI_SubmitWriteRequest();

                spiBLData.flashState = BL_FLASH_STATE_WRITE;
            }
            break;

        case BL_COMMAND_VERIFY:

            SET_BIT(spiBLData.status, BL_STATUS_BIT_BUSY);
            spiBLData.flashState = BL_FLASH_STATE_VERIFY;
            break;

        case BL_COMMAND_RESET:

            spiBLData.flashState = BL_FLASH_STATE_RESET;
            break;

        case BL_COMMAND_READ_STATUS:

            (void) SERCOM3_SPI_Write(&spiBLData.status, 1);
            CLR_BIT(spiBLData.status, BL_STATUS_BIT_ALL);
            break;

        case BL_COMMAND_READ_VERSION:

            spiBLData.btlVersion = bootloader_GetVersion();

            /* Swap Major and Minor Number */
            spiBLData.btlVersion = (((spiBLData.btlVersion << 8) & 0xFF00U) | ((spiBLData.btlVersion >> 8) & 0xFFU));

            (void) SERCOM3_SPI_Write(&spiBLData.btlVersion, 2);

            break;

        default:
            /* 0xFF is used as a dummy byte to read the status by the host */
            if (spiBLData.cmd.readBuffer[0] != 0xFFU)
            {
                SET_BIT(spiBLData.status, BL_STATUS_BIT_INVALID_COMMAND);
            }
            break;
    }

    if (IS_BIT_SET(spiBLData.status, BL_STATUS_BIT_BUSY) == false)
    {
        SERCOM3_SPI_Ready();
    }
}

static void BL_SPI_FlashTask(void)
{
    switch(spiBLData.flashState)
    {
        case BL_FLASH_STATE_ERASE:
            // Lock region size is always bigger than the row size

            /* Erase the Current row */
            (void) FCW_PageErase(spiBLData.cmd.eraseCommand.memAddr);

            spiBLData.flashState = BL_FLASH_STATE_ERASE_BUSY_POLL;

            break;

        case BL_FLASH_STATE_WRITE:
            (void) FCW_RowWrite((void *)&spiBLData.cmd.programCommand.data[spiBLData.dataBufferAlignOffset + spiBLData.nFlashBytesWritten], (spiBLData.cmd.programCommand.memAddr + spiBLData.nFlashBytesWritten));

            spiBLData.flashState = BL_FLASH_STATE_WRITE_BUSY_POLL;

            break;

        case BL_FLASH_STATE_VERIFY:
            if (bootloader_CRCGenerate(spiBLData.appImageStartAddr, (spiBLData.appImageEndAddr - spiBLData.appImageStartAddr)) != spiBLData.cmd.verifyCommand.crc)
            {
                SET_BIT(spiBLData.status, BL_STATUS_BIT_CRC_ERROR);
            }
            CLR_BIT(spiBLData.status, BL_STATUS_BIT_BUSY);
            SERCOM3_SPI_Ready();
            spiBLData.flashState = BL_FLASH_STATE_IDLE;

            break;

        case BL_FLASH_STATE_ERASE_BUSY_POLL:
            if(FCW_IsBusy() == false)
            {
                CLR_BIT(spiBLData.status, BL_STATUS_BIT_BUSY);
                SERCOM3_SPI_Ready();
                spiBLData.flashState = BL_FLASH_STATE_IDLE;
            }
            break;

        case BL_FLASH_STATE_WRITE_BUSY_POLL:
            if(FCW_IsBusy() == false)
            {
                spiBLData.nFlashBytesWritten += PAGE_SIZE;

                if (spiBLData.nFlashBytesWritten < spiBLData.cmd.programCommand.nBytes)
                {
                    spiBLData.flashState = BL_FLASH_STATE_WRITE;
                }
                else
                {
                    CLR_BIT(spiBLData.status, BL_STATUS_BIT_BUSY);
                    SERCOM3_SPI_Ready();
                    spiBLData.flashState = BL_FLASH_STATE_IDLE;
                }
            }
            break;

        case BL_FLASH_STATE_RESET:
            bootloader_TriggerReset();
            break;


        case BL_FLASH_STATE_IDLE:
            /* Do nothing */
            break;

        default:
                /* Do Nothing */
            break;
    }
}

/* MISRA C-2012 Rule 11.8 deviated below. Deviation record ID -
   H3_MISRAC_2012_R_11_8_DR_1 */
static void BL_SPI_EventHandler(uintptr_t context )
{
    if (SERCOM3_SPI_ErrorGet() == SPI_SLAVE_ERROR_NONE)
    {
        spiBLData.nReadBytes = SERCOM3_SPI_Read((void*)spiBLData.cmd.readBuffer, SERCOM3_SPI_ReadCountGet());
        if(spiBLData.nReadBytes == 0U)
        {
            SERCOM3_SPI_Ready();
        }
    }
    else
    {
        SET_BIT(spiBLData.status, BL_STATUS_BIT_COMM_ERROR);
        SERCOM3_SPI_Ready();
    }
}
/* MISRAC 2012 deviation block end */

// *****************************************************************************
// *****************************************************************************
// Section: Bootloader Global Functions
// *****************************************************************************
// *****************************************************************************

void bootloader_SPI_Tasks(void)
{
    if (spiBLInitDone == false)
    {
        spiBLData.flashState = BL_FLASH_STATE_IDLE;

        spiBLData.status = BL_STATUS_READY;

        SERCOM3_SPI_CallbackRegister(BL_SPI_EventHandler, (uintptr_t) 0);
    }

    do
    {
        if (spiBLData.nReadBytes != 0U)
        {
            spiBLData.nReadBytes = 0;

            BL_SPI_CommandParser();
        }

        BL_SPI_FlashTask();
    } while (spiBLActive);
}
