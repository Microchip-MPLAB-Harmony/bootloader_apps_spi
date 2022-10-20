---
parent: Host Application Configurations
title: Configuring SDCARD Host applications
has_children: false
has_toc: false
---

[![MCHP](https://www.microchip.com/ResourcePackages/Microchip/assets/dist/images/logo.png)](https://www.microchip.com)

# Configuring the SDCARD Host applications

## Downloading the application

To clone or download this application from Github,go to the [main page of this repository](https://github.com/Microchip-MPLAB-Harmony/bootloader_apps_spi) and then click Clone button to clone this repo or download as zip file. This content can also be download using content manager by following [these instructions](https://github.com/Microchip-MPLAB-Harmony/contentmanager/wiki)

Path of the SDCARD host applications within the repository is **apps/spi_bootloader/**

## Configuring the SDCARD Host application

### Follow below steps only when Host development kit is different than Target Development Kit

1. Open the host_app_sdcard/firmware/*.X project in MPLABX IDE

2. Open the "user.h" file as shown below:

    ![spi_bootloader_host_app_sdcard_user_ide](../../spi_bootloader/docs/images/spi_bootloader_host_app_sdcard_user_ide.png)

3. In the "user.h" file specify the Bootloader Target Device used using the predefined macros
       #define APP_SPI_BOOTLOADER_TARGET_DEVICE     SAM_D21_XPRO

    ![spi_bootloader_host_app_nvm_user_config](../../spi_bootloader/docs/images/spi_bootloader_host_app_sdcard_user_config.png)

4. Navigate to the **#if block** for the specified **Bootloader Target Device** and verify the below settings are as expected.

    - **APP_BINARY_FILE:** Name of the Application binary copied to the SDCARD
    - **APP_ERASE_PAGE_SIZE:** Erase page size of the target (target = MCU being programmed)
    - **APP_PROGRAM_PAGE_SIZE:** Program page size of the target (target = MCU being programmed).
        - The macro can either be set to the program page size or can be set to the size of the erase page size of the target.
        - In the demo example, the macro is set equal to the size of the erase page.
        - On embedded host where RAM is limited, the macro may be set to the actual program page size to reduce the RAM used to hold the program data.
            - For example, for SAM D20, the macro can be set either to 64 (program page size) or 256 (erase page size)
    - **APP_IMAGE_START_ADDR:** User application start address (This value should be same as the application start address specified in Application Configurations steps).
        - If the bootloader itself is being upgraded then the APP_IMAGE_START_ADDR must be set to 0x00 (start of bootloader)
        - Ensure that the bootloader and application are also configured with the same value of user application start address

