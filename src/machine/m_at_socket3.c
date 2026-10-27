/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of Socket 3 machines.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2025 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/chipset.h>
#include <86box/keyboard.h>
#include <86box/mem.h>
#include <86box/nvr.h>
#include <86box/pci.h>
#include <86box/dma.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/gameport.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/rom.h>
#include <86box/sio.h>
#include <86box/hdc.h>
#include <86box/port_6x.h>
#include <86box/port_92.h>
#include <86box/video.h>
#include <86box/flash.h>
#include <86box/scsi_ncr53c8xx.h>
#include <86box/hwm.h>
#include <86box/machine.h>
#include <86box/plat_unused.h>
#include <86box/sound.h>

/* VLSI 82C480 */
/*
 * IBM EduQuest Fifty-Five (Type 9615).
 *
 * Socket 3 486, VLSI VL82C480-family chipset (VL82C483/VL82C114/VL82C003),
 * SMSC FDC37C665 Super I/O with IDE (the BIOS unlocks it with 55h,55h at 3F0h
 * and programs CR0-CR4 and CRE), on-board Cirrus Logic GD5434 (the board's
 * own customised video BIOS is not dumped; the generic GD5434 BIOS is used),
 * PhoenixBIOS A486 1.01 05G in an Intel 28F001BX-T flash, Phoenix MultiKey KBC.
 *
 * ROM: roms/machines/eduquest55/BIOS_05G.BIN, the 128K image (BIOS.BIN) from
 * IBM's EDUQ_05G flash update diskette.
 */
/*
 * The board's Setup is not in the flash: F2 far-calls E000:0100 with ROMCS
 * moved off the flash (VL82C480 RAMMAP ROMMOV), i.e. into a separate mask
 * ROM (U13 "ROM BIOS") that has not been dumped. Without Setup nothing ever
 * writes a valid configuration, so the BIOS reports 174-B and lists no boot
 * devices. When the CMOS configuration checksum is bad, write what Setup
 * would: standard AT configuration bytes plus the IBM fields the BIOS reads
 * (29h boot order, 2 bits per entry: 1 diskette, 2 fixed disk, 3 network;
 * 41h bit 7 "built-in monitor off").
 */
static uint8_t
eduquest55_cmos_read(uint8_t idx)
{
    outb(0x70, idx | 0x80);
    return inb(0x71);
}

static void
eduquest55_cmos_write(uint8_t idx, uint8_t val)
{
    outb(0x70, idx | 0x80);
    outb(0x71, val);
}

static void
eduquest55_cmos_defaults(void)
{
    uint16_t sum = 0;
    uint32_t ext = (mem_size > 1024) ? (mem_size - 1024) : 0;

    for (uint8_t i = 0x10; i <= 0x2d; i++)
        sum += eduquest55_cmos_read(i);
    if (sum == ((eduquest55_cmos_read(0x2e) << 8) | eduquest55_cmos_read(0x2f)))
        return;

    if (ext > 0xffff)
        ext = 0xffff;

    for (uint8_t i = 0x10; i <= 0x2d; i++)
        eduquest55_cmos_write(i, 0x00);
    eduquest55_cmos_write(0x10, 0x40);                          /* A: 1.44M, B: none */
    eduquest55_cmos_write(0x14, 0x01 | (hasfpu ? 0x02 : 0x00)); /* 1 diskette, VGA */
    eduquest55_cmos_write(0x15, 0x80);                          /* 640K base */
    eduquest55_cmos_write(0x16, 0x02);
    eduquest55_cmos_write(0x17, ext & 0xff);
    eduquest55_cmos_write(0x18, ext >> 8);
    eduquest55_cmos_write(0x29, 0x01 | (0x02 << 2) | (0x03 << 4)); /* diskette, disk, network */

    sum = 0;
    for (uint8_t i = 0x10; i <= 0x2d; i++)
        sum += eduquest55_cmos_read(i);
    eduquest55_cmos_write(0x2e, sum >> 8);
    eduquest55_cmos_write(0x2f, sum & 0xff);

    eduquest55_cmos_write(0x41, eduquest55_cmos_read(0x41) & 0x7f); /* built-in monitor on */
    eduquest55_cmos_write(0x0e, 0x00);
}

int
machine_at_eduquest55_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/eduquest55/BIOS_05G.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add(&vl82c480_device);

    /* The FDC37C665 only decodes the IDE ports; the controller is the board's. */
    device_add(&ide_isa_device);
    device_add_params(&fdc37c6xx_device, (void *) (FDC37C665 | FDC37C6XX_IDE_PRI));

    device_add(&intel_flash_bxt_device);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    eduquest55_cmos_defaults();

    return ret;
}

/* ACC 2168 */
int
machine_at_pb430_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/pb430/PB430-Dump-2.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_ide_init(model);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add(&acc3221_device);
    device_add(&acc2168_device);

    device_add(&phoenix_486_jumper_pb430_device);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    return ret;
}

/* ALi M1429G */
int
machine_at_atc1762_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/atc1762/atc1762.bin",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);
    device_add(&ali1429g_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

int
machine_at_ecsal486_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ecsal486/ECS_AL486.BIN",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&ali1429g_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

int
machine_at_ap4100aa_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ap4100aa/M27C512DIP28.BIN",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&ali1429g_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add(&ide_vlb_device);
    device_add_params(&um866x_device, (void *) (uintptr_t) UM8663BF);

    return ret;
}

/* Contaq 82C596A */
int
machine_at_4gpv5_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/4gpv5/4GPV5.bin",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    device_add(&contaq_82c596a_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    return ret;
}

/* Contaq 82C597 */
int
machine_at_greenb_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/greenb/4gpv31-ami-1993-8273517.bin",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    device_add(&contaq_82c597_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    return ret;
}

/* OPTi 499 */
int
machine_at_xenon_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/xenon/addx-bios-7-71-i28f001.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&opti499_device);
    device_add(&ide_vlb_device);
    device_add_params(&fdc37c6xx_device, (void *) (FDC37C661 | FDC37C6XX_IDE_PRI));
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add(&intel_flash_bxt_device);

    return ret;
}

int
machine_at_cobalt_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/cobalt/Cobalt_2.3.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&opti499_device);
    device_add(machine_get_ide_device(machine));
    device_add(&ide_isa_sec_device);
    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    return ret;
}

/* OPTi 895 */
static const device_config_t j403tg_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "403tg",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "AMI WinBIOS (121593)",
                .internal_name = "403tg",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 65536,
                .files         = { "roms/machines/403tg/J403TGRevD.BIN", "" }
            },
            {
                .name          = "AwardBIOS v4.50G",
                .internal_name = "403tg_award",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 65536,
                .files         = { "roms/machines/403tg/403TG.BIN", "" }
            },
            {
                .name          = "MR BIOS V2.02",
                .internal_name = "403tg_mr",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 65536,
                .files         = { "roms/machines/403tg/MRBiosOPT895.bin", "" }
            },
            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t j403tg_device = {
    .name          = "Jetway J-403TG",
    .internal_name = "403tg",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = j403tg_config
};

int
machine_at_403tg_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;

    /* No ROMs available */
    if (!device_available(model->device))
        return ret;

    device_context(model->device);
    int nvr_hack = !strcmp(device_get_config_bios("bios"), "403tg");
    fn           = device_get_bios_file(machine_get_device(machine), device_get_config_bios("bios"), 0);
    ret          = bios_load_linear(fn, 0x000f0000, 65536, 0);

    machine_at_common_init(model);
    device_add_params(&nvr_at_device, (void *) (uintptr_t) (nvr_hack ? (NVR_AMI_1994) : (NVR_AT)));

    device_add(&opti895_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

/* SiS 461 */
int
machine_at_acerv10_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/acerv10/ALL.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&sis_85c461_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    device_add(&ide_isa_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}

/* SiS 471 */
static const device_config_t win471t_config[] = {
    // clang-format off
    {
        .name        = "cache_size",
        .description = "External cache",
        .type        = CONFIG_SELECTION,
        .default_int = 3,
        .selection   = {
            { .description = "128 KB",  .value = 2 },
            { .description = "256 KB",  .value = 3 },
            { .description = "512 KB",  .value = 4 },
            { .description = "1 MB",    .value = 5 },
            { .description = "" }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t win471t_device = {
    .name          = "ABIT AB-AH4T",
    .internal_name = "win471t",
    .flags         = 0,
    .local         = 0,
    .init          = NULL,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = win471t_config
};

static void
machine_at_sis_85c471_common_init(const machine_t *model)
{
    machine_at_common_init(model);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    device_add(&sis_85c471_device);
}

int
machine_at_win471_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/win471/486-SiS_AC0360136.BIN",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_sis_85c471_common_init(model);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    return ret;
}

int
machine_at_win471t_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/win471t/486-SiS_AB6680759.BIN",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    /* SiS 85C471 register 51h bits 6:4 are the external cache size.
     * Store the selected code in bits 18:16 of the device local value and
     * set bit 24 to indicate that this board should force those bits. */
    uint8_t cache_size_code = machine_get_config_int("cache_size");
    uintptr_t sis_params = ((uintptr_t) 1 << 24) | ((uintptr_t) cache_size_code << 16);
    device_add_params(&sis_85c471_device, (void *) sis_params);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    return ret;
}

int
machine_at_vi15g_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/vi15g/vi15gr23.rom",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_sis_85c471_common_init(model);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    return ret;
}

int
machine_at_vli486sv2g_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/vli486sv2g/0402.001",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_sis_85c471_common_init(model);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    return ret;
}

int
machine_at_dvent4xx_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/dvent4xx/Venturis466_BIOS.BIN",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);
    device_add(&sis_85c471_device);
    device_add(machine_get_ide_device(machine));
    device_add_params(&fdc37c6xx_device, (void *) (FDC37C665 | FDC37C6XX_IDE_SEC));

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    return ret;
}

int
machine_at_dtk486_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/dtk486/4siw005.bin",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_sis_85c471_common_init(model);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    return ret;
}

int
machine_at_ami471_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ami471/SIS471BE.AMI",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_sis_85c471_common_init(model);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    return ret;
}

int
machine_at_lpm30_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear_inverted("roms/machines/lpm30/n28f001bx-t-at-plcc32-miconics-lpm30.bin",
                                    0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&sis_85c471_device);
    device_add(machine_get_ide_device(machine));
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);
    device_add_params(&fdc37c6xx_device, (void *) FDC37C665);
    device_add(&intel_flash_bxt_device);

    if (sound_card_current[0] == SOUND_INTERNAL)
        machine_snd = device_add(machine_get_snd_device(machine));

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    return ret;
}

int
machine_at_px471_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/px471/SIS471A1.PHO",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_sis_85c471_common_init(model);

    device_add(&ide_vlb_device);

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    return ret;
}

int
machine_at_tg486g_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/tg486g/tg486g.bin",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);
    device_add(&sis_85c471_device);
    device_add(&ide_isa_device);
    device_add_params(&fdc37c6xx_device, (void *) (FDC37C651 | FDC37C6XX_IDE_PRI));

    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    video_reset(gfxcard[0]);

    if (gfxcard[0] != VID_INTERNAL) {
        for (uint16_t i = 0; i < 32768; i++)
            rom[i] = mem_readb_phys(0x000c0000 + i);
    }
    mem_mapping_set_addr(&bios_mapping, 0x0c0000, 0x40000);
    mem_mapping_set_exec(&bios_mapping, rom);

    return ret;
}

int
machine_at_vs486f3vl_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/vs486f3vl/vega-vs486f-3vl-060692.bin",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&sl82c461_device);
    device_add_params(machine_get_kbc_device(machine), (void *) model->kbc_params);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    return ret;
}
