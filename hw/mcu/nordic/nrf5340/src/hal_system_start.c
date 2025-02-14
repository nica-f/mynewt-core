/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <stddef.h>
#include <inttypes.h>
#include <mcu/cortex_m33.h>
#include <mcu/nrf5340_hal.h>

#if MCUBOOT_MYNEWT
#include <bootutil/bootutil.h>
#endif
#include <os/util.h>

#if MYNEWT_VAL(BOOT_LOADER) && !MYNEWT_VAL(MCU_APP_SECURE)

struct periph_id_range {
    uint8_t first;
    uint8_t last;
};

/* Array of peripheral ID ranges that will be set as unsecure before bootloader jumps to application code */
static const struct periph_id_range ns_peripheral_ids[] = {
    { 0, 0 },
    { 4, 6 },
    { 8, 12 },
    { 14, 17 },
    { 20, 21 },
    { 23, 36 },
    { 38, 38 },
    { 40, 40 },
    { 42, 43 },
    { 45, 45 },
    { 48, 48 },
    { 51, 52 },
    { 54, 55 },
    { 57, 57 },
    { 66, 66 },
    { 128, 129 },
};

void
hal_system_start(void *img_start)
{
    int i;
    int j;
    int range_count;
    struct flash_sector_range sr;
    uintptr_t *img_data;
    /* Number of 16kB flash regions used by bootloader */
    int bootloader_flash_regions;
    __attribute__((cmse_nonsecure_call, noreturn)) void (* app_reset)(void);

    /* Mark selected peripherals as unsecure */
    for (i = 0; i < ARRAY_SIZE(ns_peripheral_ids); ++i) {
        for (j = ns_peripheral_ids[i].first; j <= ns_peripheral_ids[i].last; ++j) {
            if (((NRF_SPU->PERIPHID[j].PERM & SPU_PERIPHID_PERM_PRESENT_Msk) == 0) ||
                ((NRF_SPU->PERIPHID[j].PERM & SPU_PERIPHID_PERM_SECUREMAPPING_Msk) < SPU_PERIPHID_PERM_SECUREMAPPING_UserSelectable)) {
                continue;
            }
            NRF_SPU->PERIPHID[j].PERM &= ~SPU_PERIPHID_PERM_SECATTR_Msk;
        }
    }

    /* Route exceptions to non-secure, allow software reset from non-secure */
    SCB->AIRCR = 0x05FA0000 | (SCB->AIRCR & (~SCB_AIRCR_VECTKEY_Msk | SCB_AIRCR_SYSRESETREQS_Msk)) | SCB_AIRCR_BFHFNMINS_Msk;
    for (i = 0; i < ARRAY_SIZE(NVIC->ITNS); ++i) {
        NVIC->ITNS[i] = 0xFFFFFFFF;
    }

    /* Mark non-bootloader flash regions as non-secure */
    flash_area_to_sector_ranges(FLASH_AREA_BOOTLOADER, &range_count, &sr);
    bootloader_flash_regions = (sr.fsr_sector_count * sr.fsr_sector_size) / 0x4000;

    for (i = bootloader_flash_regions; i < 64; ++i) {
        NRF_SPU->FLASHREGION[i].PERM &= ~SPU_FLASHREGION_PERM_SECATTR_Msk;
    }

    /* Mark RAM as non-secure */
    for (i = 0; i < 64; ++i) {
        NRF_SPU->RAMREGION[i].PERM &= ~SPU_FLASHREGION_PERM_SECATTR_Msk;
    }

    /* Move DPPI to non-secure area */
    NRF_SPU->DPPI->PERM = 0;

    /* Move GPIO to non-secure area */
    NRF_SPU->GPIOPORT[0].PERM = 0;
    NRF_SPU->GPIOPORT[1].PERM = 0;

    img_data = img_start;
    app_reset = (void *)(img_data[1]);
    __TZ_set_MSP_NS(img_data[0]);
    app_reset();
}

#else

/**
 * Boots the image described by the supplied image header.
 *
 * @param hdr                   The header for the image to boot.
 */
void __attribute__((naked))
hal_system_start(void *img_start)
{
    uint32_t *img_data = img_start;

    asm volatile (".syntax unified        \n"
                  /* 1st word is stack pointer */
                  "    msr  msp, %0       \n"
                  /* 2nd word is a reset handler (image entry) */
                  "    bx   %1            \n"
                  : /* no output */
                  : "r" (img_data[0]), "r" (img_data[1]));
}

#endif

/**
 * Boots the image described by the supplied image header.
 * This routine is used in split-app scenario when loader decides
 * that it wants to run the app instead.
 *
 * @param hdr                   The header for the image to boot.
 */
void
hal_system_restart(void *img_start)
{
    int i;
    int sr;

    /*
     * Disable interrupts, and leave the disabled.
     * They get re-enabled when system starts coming back again.
     */
    __HAL_DISABLE_INTERRUPTS(sr);
    for (i = 0; i < sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0]); i++) {
        NVIC->ICER[i] = 0xffffffff;
    }
    (void)sr;

    hal_system_start(img_start);
}
