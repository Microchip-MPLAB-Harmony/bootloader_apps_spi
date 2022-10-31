---
title: Release notes
nav_order: 99
---

![Microchip logo](https://raw.githubusercontent.com/wiki/Microchip-MPLAB-Harmony/Microchip-MPLAB-Harmony.github.io/images/microchip_logo.png)
![Harmony logo small](https://raw.githubusercontent.com/wiki/Microchip-MPLAB-Harmony/Microchip-MPLAB-Harmony.github.io/images/microchip_mplab_harmony_logo_small.png)

# Microchip MPLAB® Harmony 3 Release Notes

## SPI Bootloader Applications Release v3.0.0

### New Features

- This release provides SPI Bootloader Demo Applications for PIC32MM, SAM D21/DA1, SAM D5x/E5x and SAM E70/S70/V70/V71 family of 32-bit microcontrollers.

### Development kit and demo application support
- The following table provides bootloader demo applications available for different development kits.

    | Product Family                 | Development Kits                                    | SPI             | SPI Fail Safe            |
    | ------------------------------ | --------------------------------------------------- | ---------------- | ------------------------- |   
    | PIC32MM                        | [PIC32MM USB Curiosity Development Board](https://www.microchip.com/en-us/development-tool/dm320107) | Yes | NA |   
    | SAM D21/DA1                    | [SAM D21 Xplained Pro Evaluation Kit](https://www.microchip.com/DevelopmentTools/ProductDetails.aspx?PartNO=ATSAMD21-XPRO)   | Yes              | NA                        |
    | SAM D5x/E5x                    | [SAM E54 Xplained Pro Evaluation Kit](https://www.microchip.com/developmenttools/ProductDetails/ATSAME54-XPRO)               | Yes              | No                       |
    | SAM E70/S70/V70/V71            | [SAM E70 Xplained Ultra Evaluation Kit](https://www.microchip.com/DevelopmentTools/ProductDetails.aspx?PartNO=ATSAME70-XULT)   | Yes              | NA                        |

- **NA:** Lack of product capability

### Known Issues

- None

### Development Tools

- [MPLAB® X IDE v6.00](https://www.microchip.com/mplab/mplab-x-ide)
- MPLAB® X IDE plug-ins:
  - MPLAB® Code Configurator (MCC) v5.1.9
- [MPLAB® XC32 C/C++ Compiler v4.10](https://www.microchip.com/mplab/compilers)