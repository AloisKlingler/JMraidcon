/*
 * JMraidcon - A console interface to the JMicron JMB394 H/W RAID controller
 * Copyright (C) 2010 Werner Johansson, <wj@xnk.nu>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <scsi/sg.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include "jm_crc.h"
#include "sata_xor.h"
#include "jmraid.h"
#include <asm/byteorder.h> // For __le32_to_cpu etc

#define SECTORSIZE (512)
#define READ_CMD (0x28)
#define WRITE_CMD (0x2a)
#define RW_CMD_LEN (10)

#define JM_RAID_WAKEUP_CMD    ( 0x197b0325 )
//#define JM_RAID_SCRAMBLED_CMD ( 0x197b0322 ) // JMB39x
//#define JM_RAID_SCRAMBLED_CMD ( 0x197b0562 ) // JMS56x

int g_print_indent = 0;

// uint8_t g_tempBuf2[SECTORSIZE];
uint32_t g_cmdNum = 1;

// First 4 bytes are always the same for all the scrambled commands, next 4 bytes forms an incrementing command id
// (and these 8 bytes are now automatically prepended and no longer listed here)
// The Identify disk commands does not return the data in the same format as the normal IDENTIFY DEVICE!??
//const uint8_t probe16[] ={ 0x00, 0x03, 0x02, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }; // AWARD I5, wtf??  // jmraid_get_raid_port_info port 0?
const uint8_t getraidportinfo_probe[] ={ 0x00, 0x03, 0x02, 0xff, 0x00 }; // AWARD I5, wtf??  // jmraid_get_raid_port_info port 0?
//const uint8_t getraidportinfo_probe[] ={ 0x00, 0x03, 0x02, 0xff, 0x01 }; // AWARD I5, wtf??  // jmraid_get_raid_port_info port 0?
const uint8_t getchipinfo_probe[]     ={ 0x00, 0x01, 0x01, 0xff }; // jmraid_get_chip_info
const uint8_t getsatainfo_probe[]     ={ 0x00, 0x02, 0x01, 0xff }; // jmraid_get_sata_info
const uint8_t getsataport0info_probe[]={ 0x00, 0x02, 0x02, 0x00, 0x00, 0xff }; // jmraid_get_sata_port_info port 0
const uint8_t getsataport1info_probe[]={ 0x00, 0x02, 0x02, 0x00, 0x01, 0xff }; // jmraid_get_sata_port_info port 1
const uint8_t disk0smartread1_probe[]={ 0x00, 0x02, 0x03, 0xff, 0x00, 0x02, 0x00, 0xe0, 0x00, 0x00,         // disk0 ata passthrough
        0xd0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4f, 0x00, 0xc2, 0x00, 0xa0, 0x00, 0xb0, 0x00 };               // SMART READ ATTRIBUTE VALUE ata cmd
const uint8_t disk0smartread2_probe[]={ 0x00, 0x02, 0x03, 0xff, 0x00, 0x02, 0x00, 0xe0, 0x00, 0x00,         // disk0 ata passthrough, again
        0xd1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4f, 0x00, 0xc2, 0x00, 0xa0, 0x00, 0xb0, 0x00 };               // SMART READ ATTRIBUTE THRESHOLDS ata cmd
const uint8_t disk1smartread1_probe[]={ 0x00, 0x02, 0x03, 0xff, 0x01, 0x02, 0x00, 0xe0, 0x00, 0x00,         // disk1 ata passthrough
        0xd0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4f, 0x00, 0xc2, 0x00, 0xa0, 0x00, 0xb0, 0x00 };               // SMART READ ATTRIBUTE VALUE ata cmd
const uint8_t disk1smartread2_probe[]={ 0x00, 0x02, 0x03, 0xff, 0x01, 0x02, 0x00, 0xe0, 0x00, 0x00,         // disk1 ata passthrough, again
        0xd1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4f, 0x00, 0xc2, 0x00, 0xa0, 0x00, 0xb0, 0x00 };               // SMART READ ATTRIBUTE THRESHOLDS ata cmd


    sg_io_hdr_t io_hdr;
#warning FIXME: Should not use a hard-coded sector number (0x21) (or 0xfe), even though it is backed up and restored afterwards
    uint8_t rwCmdBlk[RW_CMD_LEN] =
                    {READ_CMD, 0x00, 0x00, 0x00, 0x00, 0xfe, 0x00, 0x00, 0x01, 0x00}; // SECTOR NUMBER 0xfe!!!!!!

uint32_t Do_JM_Cmd( int theFD, uint32_t* theCmd, uint32_t* theResp ) {
    uint32_t retval=0;

    // Calculate CRC for the request
    uint32_t myCRC = JM_CRC( theCmd, 0x7f );

    // Stash the CRC at the end
    theCmd[0x7f] = __cpu_to_le32( myCRC );
//    printf("Command CRC: 0x%08x\n", myCRC);

    // Make the data look really 31337 (or not)
    SATA_XOR( theCmd );

    io_hdr.dxfer_direction = SG_DXFER_TO_DEV;
    rwCmdBlk[0] = WRITE_CMD;
    io_hdr.dxferp = theCmd;
    ioctl(theFD, SG_IO, &io_hdr);

    io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
    rwCmdBlk[0] = READ_CMD;
    io_hdr.dxferp = theResp;
    ioctl(theFD, SG_IO, &io_hdr);

    // Make the 31337-looking response sane
    SATA_XOR( theResp );

    myCRC = JM_CRC( theResp, 0x7f);
    if( myCRC != __le32_to_cpu( theResp[0x7f] ) ) {
        printf( "Warning: Response CRC 0x%08x does not match the calculated 0x%08x!!\n", __le32_to_cpu( theResp[0x7f] ), myCRC );
        retval=1;
    }
    return retval;
}

void send_cmd(
        int theFD,
        uint32_t scrambled_cmd,
        uint8_t* theCmd,
        uint32_t theLen,
        uint8_t* resultBuf)
{
    uint8_t tempBuf1[SECTORSIZE];
    uint32_t* tempBuf1_32 = (uint32_t*)tempBuf1;

    // Entire sector is always sent, so zero fill cmd
    memset( tempBuf1, 0, SECTORSIZE );
    memcpy( tempBuf1+0x08, theCmd, theLen );

    tempBuf1_32[0] = __cpu_to_le32( scrambled_cmd );
    tempBuf1_32[1] = __cpu_to_le32( g_cmdNum++ );
    theLen+=0x08; // Adding the SCRAMBLED_CMD and command number

    Do_JM_Cmd( theFD, (uint32_t*)tempBuf1, (uint32_t*)resultBuf);
}
 
void process_cmd(
        int theFD,
        uint32_t scrambled_cmd,
        uint8_t* theCmd,
        uint32_t theLen,
        uint8_t result_offset,
        void (*parse_and_print)(const uint8_t*)) {
    const uint8_t *resultBuf = malloc(SECTORSIZE);
    send_cmd(theFD, scrambled_cmd, theCmd, theLen, resultBuf);
    const uint8_t *info = resultBuf + result_offset;
    (*parse_and_print)(info);
    free(resultBuf);
}

void print(const char* format, ...)
{
  va_list arglist;
  int len = g_print_indent * 2;
  while (len-- > 0)
  {
    putchar(' ');
  }
  va_start(arglist, format);
  vprintf(format, arglist);
  va_end(arglist);
}
const char *get_raid_rebuild_priority_text(uint16_t raid_rebuild_priority)
{
  // 0x4000 = low, 0x2000 = low-middle, 0x1000 = middle, 0x0800 = middle-high, 0x0400 = high
  if (raid_rebuild_priority <= ((0x0400 + 0x0800) / 2))
  {
    return "Highest";
  }
  else if (raid_rebuild_priority <= ((0x0800 + 0x1000) / 2))
  {
    return "High";
  }
  else if (raid_rebuild_priority <= ((0x1000 + 0x2000) / 2))
  {
    return "Medium";
  }
  else if (raid_rebuild_priority <= ((0x2000 + 0x4000) / 2))
  {
    return "Low";
  }
  else
  {
    return "Lowest";
  }
}

const char *get_raid_level_text(uint8_t raid_level)
{
  switch (raid_level)
  {
  case 0x00: return "RAID 0";
  case 0x01: return "RAID 1";
  case 0x02: return "JBOD / LARGE";
  case 0x03: return "RAID 3";
  case 0x04: return "CLONE";
  case 0x05: return "RAID 5";
  case 0x06: return "RAID 10";
  default: return "?";
  }
}

const char *get_raid_state_text(uint8_t raid_state)
{
  switch (raid_state)
  {
    case 0x00: return "Broken";
    case 0x01: return "Degraded";
    case 0x02: return "Rebuilding";
    case 0x03: return "Normal";
    case 0x04: return "Expansion";
    case 0x05: return "Backup";
    default: return "?";
  }
}

static uint32_t read_u32_le(const uint8_t *p)
{
  return (p[0] << 0) | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

static uint16_t read_u16_le(const uint8_t *p)
{
  return (p[0] << 0) | (p[1] << 8);
}

static void swap_bytes(uint8_t *data, uint32_t size)
{
  while (size > 1)
  {
    uint8_t temp;
    temp = data[0];
    data[0] = data[1];
    data[1] = temp;
    data += 2;
    size -= 2;
  }
}

void parse_jmraid_raid_port_info(const uint8_t *src, struct jmraid_raid_port_info *dst) {
  const uint8_t *p = src;
  int i;

  memset(dst, 0, sizeof(struct jmraid_raid_port_info));

  p += 0x04;
  dst->port_state = p[0x40];
  memcpy(dst->model_name, p + 0x00, 0x28);
  swap_bytes(dst->model_name, 0x28);
  memcpy(dst->serial_number, p + 0x28, 0x14);
  swap_bytes(dst->serial_number, 0x14);
  dst->level = p[0x50];
  dst->capacity = ((uint64_t)read_u32_le(p + 0x3C)) * (32 * 1024 * 1024);
  dst->state = p[0x42];
  dst->member_count = p[0x51];
  dst->rebuild_priority = read_u16_le(p + 0x60);
  dst->standby_timer = read_u16_le(p + 0x62) * 10;
  memcpy(dst->password, p + 0x78, 0x08);
  dst->rebuild_progress =
      ((uint64_t)read_u32_le(p + 0x5C)) * (32 * 1024 * 1024);

  p += 0xA0;
  for (i = 0; i < 5; i++) {
    struct jmraid_raid_port_info_member *member = &dst->member[i];
    member->ready = p[0x00];
    member->lba48_support = p[0x04];
    member->sata_page = p[0x06];
    member->sata_port = p[0x07];
    member->sata_base = read_u32_le(p + 0x08);
    member->sata_size = ((uint64_t)read_u32_le(p + 0x0C)) * (32 * 1024 * 1024);
    p += 0x20;
  }
}

void print_raid_port_info(const struct jmraid_raid_port_info *info)
{
  if (info->port_state != 0x00)
  {
    int i;
    print("Model name       = %s\n", info->model_name);
    print("Serial number    = %s\n", info->serial_number);
    print("Port state       = %d \n", info->port_state);
    print("Level            = %d (%s)\n", info->level, get_raid_level_text(info->level));
    print("Capacity         = %.2f GB\n", (float)info->capacity / (1 * 1024 * 1024 * 1024));
    print("State            = %d (%s)\n", info->state, get_raid_state_text(info->state));
    print("Member count     = %d\n", info->member_count);
    print("Rebuild priority = %d (%s)\n", info->rebuild_priority, get_raid_rebuild_priority_text(info->rebuild_priority));
    print("Standby timer    = %d sec\n", info->standby_timer);
    print("Password         = %s\n", info->password);
    print("Rebuild progress = %.2f %%\n", info->capacity ? (float)info->rebuild_progress * 100 / info->capacity : 0);
    for (i = 0; i < info->member_count; i++)
    {
      const struct jmraid_raid_port_info_member *member = &info->member[i];
      print("\n");
      print("Member %d\n", i);
      g_print_indent++;
      print("Ready         = %d\n", member->ready);
      print("LBA48 support = %d\n", member->lba48_support);
      print("SATA port     = %d\n", member->sata_port);
      print("SATA page     = %d\n", member->sata_page);
      print("SATA base     = %d\n", member->sata_base);
      print("SATA size     = %.2f GB\n", (float)member->sata_size / (1 * 1024 * 1024 * 1024));
      g_print_indent--;
    }
  }
  else
  {
    print("Port state = %d \n", info->port_state);
  }
}

//Alois start

const char *get_sata_port_speed_text(uint8_t sata_port_speed)
{
        switch (sata_port_speed)
        {
                case 0x00: return "No Connection";
                case 0x01: return "Gen 1";
                case 0x02: return "Gen 2";
                case 0x03: return "Gen 3";
                default: return "?";
        }
}

const char *get_sata_port_type_text(uint8_t sata_port_type)
{
        switch (sata_port_type)
        {
                case 0x00: return "No Device";
                case 0x01: return "Hard Disk";
                case 0x02: return "RAID Disk";
                case 0x03: return "Optical Drive";
                case 0x04: return "Bad Port";
                case 0x05: return "Skip";
                case 0x06: return "Off";
                case 0x07: return "Host";
                default: return "?";
        }
}

const char *get_sata_page_state_text(uint8_t sata_page_state)
{
        switch (sata_page_state)
        {
                case 0x00: return "Null";
                case 0x01: return "Valid";
                case 0x02: return "Hooked to PM";
                case 0x03: return "Spare Drive";
                case 0x04: return "Bad Page";
                default: return "?";
        }
}

const char *get_smart_attribute_name(uint8_t id)
{
        switch (id)
        {
                case 0x01: return "Raw Read Error Rate";
                case 0x02: return "Throughput Performance";
                case 0x03: return "Spin Up Time";
                case 0x04: return "Start/Stop Count";
                case 0x05: return "Reallocated Sectors Count";
                case 0x06: return "Read Channel Margin";
                case 0x07: return "Seek Error Rate";
                case 0x08: return "Seek Time Performance";
                case 0x09: return "Power-On Time Count";
                case 0x0A: return "Spin Retry Count";
                case 0x0B: return "Drive Calibration Retry Count";
                case 0x0C: return "Drive Power Cycle Count";
                case 0x0D: return "Soft Read Error Rate";
                case 0xA0: return "Uncorrectable Error Cnt";
                case 0xA1: return "Valid Spare Block Cnt";
                case 0xA3: return "Initial Bad Block Count";
                case 0xA4: return "Total Erase Count";
                case 0xA5: return "Max Erase Count";
                case 0xA6: return "Min Erase Count";
                case 0xA7: return "Average Erase Count";
                case 0xA8: return "Max Erase Count of Spec";
                case 0xA9: return "Remaining Lifetime Perc";
                case 0xAF: return "Program_Fail_Count_Chip";
                case 0xB0: return "Erase Fail Count Chip";
                case 0xB1: return "Wear Leveling Count";
                case 0xB2: return "Runtime Invalid Blk Cnt";
                case 0xB5: return "Program Fail Cnt Total";
                case 0xB6: return "Erase Fail Count Total";
                case 0xB7: return "SATA Downshift Count";
                case 0xB8: return "End-to-End_Error";
                case 0xBB: return "Reported_Uncorrect";
                case 0xBC: return "Command_Timeout";
                case 0xBD: return "High_Fly_Writes";
                case 0xBE: return "Airflow_Temperature_Celsius";
                case 0xBF: return "G-Sense_Error_Rate";
                case 0xC0: return "Power off Retract Cycle";
                case 0xC1: return "Load/Unload Cycle Count";
                case 0xC2: return "HDD Temperature";
                case 0xC3: return "Hardware ECC Recovered";
                case 0xC4: return "Reallocation Event Count";
                case 0xC5: return "Current Pending Sector Count";
                case 0xC6: return "Off-Line Uncorrectable Sector Count";
                case 0xC7: return "Ultra ATA CRC Error Count";
                case 0xC8: return "Write Error Rate";
                case 0xE8: return "Available Reservd Space";
                case 0xF0: return "Head_Flying_Hours";
                case 0xF1: return "Host Writes 32MiB";
                case 0xF2: return "Host Reads 32MiB";
                case 0xF5: return "TLC Writes 32MiB";
                default: return "?";
        }
}


/*
int jmraid_invoke_command_get_chip_info(struct jmraid *jmraid, uint8_t *data_out, uint32_t size_out)
{
        uint8_t data_in[2];

        data_in[0] = 0x01;
        data_in[1] = 0x01;

        if (!jmraid_invoke_command(jmraid, data_in, sizeof(data_in), data_out, size_out))
        {
                return 1;
        }

        return 0;
}

int jmraid_invoke_command_get_sata_info(struct jmraid *jmraid, uint8_t *data_out, uint32_t size_out)
{
        uint8_t data_in[2];

        data_in[0] = 0x02;
        data_in[1] = 0x01;

        if (!jmraid_invoke_command(jmraid, data_in, sizeof(data_in), data_out, size_out))
        {
                return 1;
        }

        return 0;
}

int jmraid_invoke_command_get_sata_port_info(struct jmraid *jmraid, uint8_t sata_port, uint8_t *data_out, uint32_t size_out)
{
        uint8_t data_in[3];

        data_in[0] = 0x02;
        data_in[1] = 0x02;
        data_in[2] = sata_port;

        if (!jmraid_invoke_command(jmraid, data_in, sizeof(data_in), data_out, size_out))
        {
                return 1;
        }

        return 0;
}
*/

void print_chip_info(const struct jmraid_chip_info *info)
{
        print("Firmware version = %02d.%02d.%02d.%02d\n", info->firmware_version[3], info->firmware_version[2], info->firmware_version[1], info->firmware_version[0]);
        print("Manufacturer     = %s\n", info->manufacturer);
        print("Product name     = %s\n", info->product_name);
        print("Serial number    = %d\n", info->serial_number);
}

/*
int jmraid_get_chip_info(struct jmraid *jmraid, struct jmraid_chip_info *info)
{
        uint8_t data_out[SECTORSIZE];


        if (!jmraid_invoke_command_get_chip_info(jmraid, data_out, sizeof(data_out)))
        {
                return 1;
        }

        parse_jmraid_chip_info(data_out, info);

        return 0;
}*/

void parse_jmraid_chip_info(const uint8_t *src, struct jmraid_chip_info *dst)
{
        const uint8_t *p = src;

        memset(dst, 0, sizeof(struct jmraid_chip_info));

        dst->firmware_version[0] = p[0];
        dst->firmware_version[1] = p[1];
        dst->firmware_version[2] = p[2];
        dst->firmware_version[3] = p[3];
        memcpy(dst->product_name, p + 0x14, 0x20);
        memcpy(dst->manufacturer, p + 0x34, 0x20);
        dst->serial_number = read_u32_le(p + 0xA0);
}

/*
int jmraid_get_sata_port_info(struct jmraid *jmraid, uint8_t index, struct jmraid_sata_port_info *info)
{
        uint8_t data_out[SECTORSIZE];

        if (!jmraid_invoke_command_get_sata_port_info(jmraid, index, data_out, sizeof(data_out)))
        {
                return 1;
        }

        parse_jmraid_sata_port_info(data_out, info);

        return 0;
}
*/

void parse_jmraid_sata_info(const uint8_t *src, struct jmraid_sata_info *dst)
{
        const uint8_t *p = src;
        int i;

        memset(dst, 0, sizeof(struct jmraid_sata_info));

        p += 0x04;
        for (i = 0; i < 5; i++)
        {
                struct jmraid_sata_info_item *item = &dst->item[i];
                memcpy(item->model_name, p + 0x00, 0x28);
                swap_bytes(item->model_name, 0x28);
                memcpy(item->serial_number, p + 0x28, 0x14);
                swap_bytes(item->serial_number, 0x14);
                item->capacity = ((uint64_t)read_u32_le(p + 0x3C)) * (32 * 1024 * 1024);
                item->port_type = p[0x48];
                item->port_speed = p[0x4A];
                item->page_0_state = p[0x41];
                item->page_0_raid_index = p[0x42];
                item->page_0_raid_member_index = p[0x43];
                item->port = p[0x49];
                p += 0x50;
        }
}

void print_sata_info(const struct jmraid_sata_info *info)
{
        int i;
        //for (i = 0; i < 5; i++)
        for (i = 0; i < 2; i++)
        {
                const struct jmraid_sata_info_item *item = &info->item[i];
                if (i > 0)
                {
                        print("\n");
                }
                print("SATA Port %d\n", i);
                //print("\n");
                g_print_indent++;
                if ((item->port_type == 0x01) || (item->port_type == 0x02))
                {
                        print("Model name        = %s\n", item->model_name);
                        print("Serial number     = %s\n", item->serial_number);
                        print("Capacity          = %.2f GB\n", (float)item->capacity / (1 * 1024 * 1024 * 1024));
                        print("Port type         = %d (%s)\n", item->port_type, get_sata_port_type_text(item->port_type));
                        print("Port speed        = %d (%s)\n", item->port_speed, get_sata_port_speed_text(item->port_speed));
                        print("Page 0 state      = %d (%s)\n", item->page_0_state, get_sata_page_state_text(item->page_0_state));
                        print("RAID index        = %d\n", item->page_0_raid_index);
                        print("RAID member index = %d\n", item->page_0_raid_member_index);
                        print("Port              = %d\n", item->port);
                }
                else
                {
                        print("Port type = %d (%s)\n", item->port_type, get_sata_port_type_text(item->port_type));
                }
                g_print_indent--;
        }
}

void parse_jmraid_sata_port_info(const uint8_t *src, struct jmraid_sata_port_info *dst)
{
        const uint8_t *p = src;

        memset(dst, 0, sizeof(struct jmraid_sata_port_info));

        p += 0x04;
        memcpy(dst->model_name, p + 0x00, 0x28);
        swap_bytes(dst->model_name, 0x28);
        memcpy(dst->serial_number, p + 0x28, 0x14);
        swap_bytes(dst->serial_number, 0x14);
        memcpy(dst->firmware_version, p + 0x40, 0x08);
        swap_bytes(dst->firmware_version, 0x08);
        dst->capacity = ((uint64_t)read_u32_le(p + 0x3C)) * (32 * 1024 * 1024);
        dst->port_type = p[0x60];
        dst->port = p[0x5A];
        dst->capacity_used = ((uint64_t)read_u32_le(p + 0xCC)) * (32 * 1024 * 1024);
        dst->page_0_state = p[0xBD];
        dst->page_0_raid_index = p[0xBE];
        dst->page_0_raid_member_index = p[0xBF];
}

void print_sata_port_info(const struct jmraid_sata_port_info *info)
{
        if ((info->port_type == 0x01) || (info->port_type == 0x02))
        {
                g_print_indent++;
                print("Model name        = %s\n", info->model_name);
                print("Serial number     = %s\n", info->serial_number);
                print("Firmware version  = %s\n", info->firmware_version);
                print("Capacity          = %.2f GB\n", (float)info->capacity / (1 * 1024 * 1024 * 1024));
                print("Capacity used     = %.2f GB\n", (float)info->capacity_used / (1 * 1024 * 1024 * 1024));
                print("Port type         = %d (%s)\n", info->port_type, get_sata_port_type_text(info->port_type));
                print("Port              = %d\n", info->port);
                print("Page 0 state      = %d (%s)\n", info->page_0_state, get_sata_page_state_text(info->page_0_state));
                print("RAID index        = %d\n", info->page_0_raid_index);
                print("RAID member index = %d\n", info->page_0_raid_member_index);
                g_print_indent--;
        }
        else
        {
                print("Port type = %d (%s)\n", info->port_type, get_sata_port_type_text(info->port_type));
        }
}

/* SMART
int jmraid_get_disk_smart_info(struct jmraid *jmraid, uint8_t sata_port, struct jmraid_disk_smart_info *info)
{
        uint8_t data_in[16];
        uint8_t data_out_1[SECTORSIZE];
        uint8_t data_out_2[SECTORSIZE];

        memset(data_in, 0, sizeof(data_in));
        data_in[2] = 0xD0;
        data_in[8] = 0x4F;
        data_in[10] = 0xC2;
        data_in[12] = 0xA0;
        data_in[14] = 0xB0;

        if (!jmraid_invoke_command_ata_passthrough(jmraid, sata_port, 0x00, 0xE0, data_in, data_out_1, sizeof(data_out_2)))
        {
                return 1;
        }

        memset(data_in, 0, sizeof(data_in));
        data_in[2] = 0xD1;
        data_in[8] = 0x4F;
        data_in[10] = 0xC2;
        data_in[12] = 0xA0;
        data_in[14] = 0xB0;

        if (!jmraid_invoke_command_ata_passthrough(jmraid, sata_port, 0x00, 0xE0, data_in, data_out_2, sizeof(data_out_2)))
        {
                return 1;
        }

        parse_jmraid_disk_smart_info(data_out_1, data_out_2, info);

        return 0;
}

int jmraid_invoke_command_ata_passthrough(struct jmraid *jmraid, uint8_t sata_port, uint8_t ata_read_addr, uint8_t ata_read_size, const uint8_t *ata_data, uint8_t *data_out, uint32_t size_out)
{
        uint8_t data_in[22];

        data_in[0] = 0x02;
        data_in[1] = 0x03;
        data_in[2] = sata_port;
        data_in[3] = 0x02; // ?
        data_in[4] = ata_read_addr;
        data_in[5] = ata_read_size;
        memcpy(data_in + 6, ata_data, 16);

        if (!jmraid_invoke_command(jmraid, data_in, sizeof(data_in), data_out, size_out))
        {
                return 1;
        }

        return 0;
}
*/

void parse_jmraid_disk_smart_info(const uint8_t *src1, const uint8_t *src2, struct jmraid_disk_smart_info *dst)
{

        memset(dst, 0, sizeof(struct jmraid_disk_smart_info));

        if (src1)
        {
                const uint8_t *p = src1;
                int i;

                p += 0x14;
                p += 0x02;
                for (i = 0; i < 30; i++)
                {
                        // 00h | 1 | Attribute ID Number (01h to FFh)
                        // 01h | 2 | Status Flags 2
                        // 03h | 1 | Attribute Value (valid values from 01h to FDh)
                        // 04h | 8 | Vendor specific
                        if (p[0] != 0)
                        {
                                struct jmraid_disk_smart_info_attribute *attribute = &dst->attribute[i];
                                attribute->id = p[0];
                                attribute->flags = read_u16_le(p + 1);
                                attribute->current_value = p[3];
                                attribute->worst_value = p[4];
                                attribute->raw_value = read_u32_le(p + 5) | ((uint64_t)read_u16_le(p + 9) << 32);
                        }
                        p += 0x0C;
                }
        }

        if (src2)
        {
                const uint8_t *p = src2;
                int i;

                p += 0x14;
                p += 0x02;
                for (i = 0; i < 30; i++)
                {
                        if (p[0] != 0)
                        {
                                struct jmraid_disk_smart_info_attribute *attribute = &dst->attribute[i];
                                attribute->threshold = p[1];
                        }
                        p += 0x0C;
                }
        }
}

void print_disk_smart_info(const struct jmraid_disk_smart_info *info)
{
        int i;
        for (i = 0; i < 30; i++)
        {
                const struct jmraid_disk_smart_info_attribute *attr = &info->attribute[i];
                if (attr->id != 0)
                {
                        print("%3u | %04X | %3u | %3u | %3u | %012llX | %15llu | %s\n", attr->id, attr->flags, attr->threshold, attr->current_value, attr->worst_value, attr->raw_value, attr->raw_value, get_smart_attribute_name(attr->id));
                }
        }
}


//Alois stop

//static void GETCHIPINFO( int theFD, uint32_t scrambled_cmd, uint8_t* theCmd, uint32_t theLen) {
//    uint8_t tempBuf1[SECTORSIZE];
//    uint32_t* tempBuf1_32 = (uint32_t*)tempBuf1;
//    uint8_t tempBuf2[SECTORSIZE];
//
//    // Entire sector is always sent, so zero fill cmd
//    memset( tempBuf1, 0, SECTORSIZE );
//    memcpy( tempBuf1+0x08, theCmd, theLen );
//
//    //tempBuf1_32[0] = __cpu_to_le32( JM_RAID_SCRAMBLED_CMD );
//    tempBuf1_32[0] = __cpu_to_le32( scrambled_cmd );
//    tempBuf1_32[1] = __cpu_to_le32( g_cmdNum++ );
//    theLen+=0x08; // Adding the SCRAMBLED_CMD and command number
//
//    Do_JM_Cmd( theFD, (uint32_t*)tempBuf1, (uint32_t*)tempBuf2 );
//
//    // all values from jmraid.c + 0x10
//    const uint8_t jmraid_offset = 0x10 - 0x04;
//    const uint8_t *info = tempBuf2 + jmraid_offset;
//
//    struct jmraid_chip_info chip_info;
//    parse_jmraid_chip_info(info, &chip_info);
//    print_chip_info(&chip_info);
//
//}
//
//static void GETSATAPORTINFO( int theFD, uint32_t scrambled_cmd, uint8_t* theCmd, uint32_t theLen) {
//    uint8_t tempBuf1[SECTORSIZE];
//    uint32_t* tempBuf1_32 = (uint32_t*)tempBuf1;
//    uint8_t tempBuf2[SECTORSIZE];
//
//    // Entire sector is always sent, so zero fill cmd
//    memset( tempBuf1, 0, SECTORSIZE );
//    memcpy( tempBuf1+0x08, theCmd, theLen );
//
//    //tempBuf1_32[0] = __cpu_to_le32( JM_RAID_SCRAMBLED_CMD );
//    tempBuf1_32[0] = __cpu_to_le32( scrambled_cmd );
//    tempBuf1_32[1] = __cpu_to_le32( g_cmdNum++ );
//    theLen+=0x08; // Adding the SCRAMBLED_CMD and command number
//
//    Do_JM_Cmd( theFD, (uint32_t*)tempBuf1, (uint32_t*)tempBuf2 );
//
//    // all values from jmraid.c + 0x10
//    const uint8_t jmraid_offset = 0x10 - 0x04;
//    const uint8_t *info = tempBuf2 + jmraid_offset;
//
//    struct jmraid_sata_port_info sata_port_info;
//    parse_jmraid_sata_port_info(info, &sata_port_info);
//    print_sata_port_info(&sata_port_info);
//
//}
//
//static void GETSATAINFO( int theFD, uint32_t scrambled_cmd, uint8_t* theCmd, uint32_t theLen) {
//    uint8_t tempBuf1[SECTORSIZE];
//    uint32_t* tempBuf1_32 = (uint32_t*)tempBuf1;
//    uint8_t tempBuf2[SECTORSIZE];
//
//    // Entire sector is always sent, so zero fill cmd
//    memset( tempBuf1, 0, SECTORSIZE );
//    memcpy( tempBuf1+0x08, theCmd, theLen );
//
//    //tempBuf1_32[0] = __cpu_to_le32( JM_RAID_SCRAMBLED_CMD );
//    tempBuf1_32[0] = __cpu_to_le32( scrambled_cmd );
//    tempBuf1_32[1] = __cpu_to_le32( g_cmdNum++ );
//    theLen+=0x08; // Adding the SCRAMBLED_CMD and command number
//
//    Do_JM_Cmd( theFD, (uint32_t*)tempBuf1, (uint32_t*)tempBuf2 );
//
//    // all values from jmraid.c + 0x10
//    const uint8_t jmraid_offset = 0x10 - 0x04;
//    const uint8_t *info = tempBuf2 + jmraid_offset;
//
//    struct jmraid_sata_info sata_info;
//    parse_jmraid_sata_info(info, &sata_info);
//    print_sata_info(&sata_info);
//}
//
//static uint8_t GETSMARTINFO( int theFD, uint32_t scrambled_cmd, uint8_t* theCmd, uint32_t theLen) {
//    uint8_t tempBuf1[SECTORSIZE];
//    uint32_t* tempBuf1_32 = (uint32_t*)tempBuf1;
//    uint8_t tempBuf2[SECTORSIZE];
//
//    // TODO (Elmar): why is this static, what does it do?
//    // original code: https://github.com/Vlad1mir-D/JMraidcon/blob/a7e438a98999f29a33bab5ea3e19504128a7ca63/src/JMraidcon.c#L136
////     static uint32_t cmdNum = 1;
//    // Elmar: moved this to global g_cmdNum variable on 2022-12-10 to test whether this makes a difference...
//
//    // Entire sector is always sent, so zero fill cmd
//    memset( tempBuf1, 0, SECTORSIZE );
//    memcpy( tempBuf1+0x08, theCmd, theLen );
//
//    //tempBuf1_32[0] = __cpu_to_le32( JM_RAID_SCRAMBLED_CMD );
//    tempBuf1_32[0] = __cpu_to_le32( scrambled_cmd );
//    tempBuf1_32[1] = __cpu_to_le32( g_cmdNum++ );
//    theLen+=0x08; // Adding the SCRAMBLED_CMD and command number
//
//    Do_JM_Cmd( theFD, (uint32_t*)tempBuf1, (uint32_t*)tempBuf2 );
//
//    // all values from jmraid.c + 0x10
//    const uint8_t jmraid_offset = 0x10 - 0x04;
//    const uint8_t *info = tempBuf2 + jmraid_offset;
//
//    struct jmraid_disk_smart_info disk_smart_info;
//    //parse_jmraid_disk_smart_info(info, &disk_smart_info);
//    //for (int i = 0; i < 2; i++) {
//    //    jmraid_get_disk_smart_info(info, i, &disk_smart_info);
//    //    print_disk_smart_info(&disk_smart_info);
//    //}
//    //jmraid_get_disk_smart_info(info, &disk_smart_info);
//    //parse_jmraid_disk_smart_info(info, &disk_smart_info);
//
//}
//
//static const uint8_t* SENDCMD(int theFD, uint32_t scrambled_cmd, uint8_t* theCmd, uint32_t theLen) {
//    uint8_t tempBuf1[SECTORSIZE];
//    uint32_t* tempBuf1_32 = (uint32_t*)tempBuf1;
////     uint8_t tempBuf2[SECTORSIZE];
//
//    // Entire sector is always sent, so zero fill cmd
//    memset( tempBuf1, 0, SECTORSIZE );
//    memcpy( tempBuf1+0x08, theCmd, theLen );
//
//    tempBuf1_32[0] = __cpu_to_le32( scrambled_cmd );
//    tempBuf1_32[1] = __cpu_to_le32( g_cmdNum++ );
//    theLen+=0x08; // Adding the SCRAMBLED_CMD and command number
//
//    Do_JM_Cmd( theFD, (uint32_t*)tempBuf1, (uint32_t*)g_tempBuf2 );
//
//    // all values from jmraid.c + 0x10
//    //const uint8_t jmraid_offset = 0x10 - 0x04;
//    const uint8_t jmraid_offset = 0xC;
//    const uint8_t *info = g_tempBuf2 + jmraid_offset;
//
//    //hexdump( tempBuf2, sizeof(tempBuf2) );
//    //printf("\n");
//    //hexdump( info, sizeof(tempBuf2)-jmraid_offset );
//    //printf("\n");
//
//    return info;
//}
//
//static void GETRAIDPORTINFO( int theFD, uint32_t scrambled_cmd, uint8_t* theCmd, uint32_t theLen) {
//    uint8_t tempBuf1[SECTORSIZE];
//    uint32_t* tempBuf1_32 = (uint32_t*)tempBuf1;
//    uint8_t tempBuf2[SECTORSIZE];
////     static uint32_t cmdNum = 1;
//
//    // Entire sector is always sent, so zero fill cmd
//    memset( tempBuf1, 0, SECTORSIZE );
//    memcpy( tempBuf1+0x08, theCmd, theLen );
//
//    //tempBuf1_32[0] = __cpu_to_le32( JM_RAID_SCRAMBLED_CMD );
//    tempBuf1_32[0] = __cpu_to_le32( scrambled_cmd );
//    tempBuf1_32[1] = __cpu_to_le32( g_cmdNum++ );
//    theLen+=0x08; // Adding the SCRAMBLED_CMD and command number
//
////    printf( "Sending command...\n");
////    hexdump( tempBuf1, (theLen+0x0f)&0x1f0 );
//    Do_JM_Cmd( theFD, (uint32_t*)tempBuf1, (uint32_t*)tempBuf2 );
////    printf( "Got response \\o/\n");
////    hexdump(tempBuf2, SECTORSIZE);
//
//    // all values from jmraid.c + 0x10
//    const uint8_t jmraid_offset = 0x10 - 0x04;
//    const uint8_t *info = tempBuf2 + jmraid_offset;
//
//    struct jmraid_raid_port_info raid_port_info;
//    parse_jmraid_raid_port_info(info, &raid_port_info);
//    print_raid_port_info(&raid_port_info);
//
////    const uint8_t model_name_len = 0x28;
////    uint8_t model_name[model_name_len+1];
////    memcpy(model_name, info + 0x00, model_name_len);
////    swap_bytes(model_name, model_name_len);
////    printf("Model name: >>%s<<\n", model_name);
////
////    const uint8_t serial_number_len = 0x14;
////    uint8_t serial_number[serial_number_len+1];
////    memcpy(serial_number, info + 0x28, serial_number_len);
////    swap_bytes(serial_number, serial_number_len);
////    printf("Serial number: >>%s<<\n", serial_number);
////
////    const uint8_t password_len = 0x08;
////    uint8_t password[password_len+1];
////    memcpy(password, info + 0x78, password_len);
////    swap_bytes(password, password_len);
////    printf("Password: >>%s<<\n", password);
////
////    printf("Member count: %d\n", info[0x51]);
//////    const uint64_t capacity = ((uint64_t)read_u32_le(p + 0x3C)) * (32 * 1024 * 1024);
////
////    uint8_t level = info[0x50];
////    printf("RAID level: %d\n", level);
////
////    uint8_t raid_state = info[0x42];  // is 0x28 in `jmraid.c` [diff = 42]
////    printf("RAID state: %s\n", get_raid_state_text(raid_state));
//}

void parse_and_print_jmraid_chip_info(const uint8_t *info) {
    struct jmraid_chip_info chip_info;
    parse_jmraid_chip_info(info, &chip_info);
    print_chip_info(&chip_info);
}

void parse_and_print_raid_port_info(const uint8_t *info) {
    struct jmraid_raid_port_info raid_port_info;
    parse_jmraid_raid_port_info(info, &raid_port_info);
    print_raid_port_info(&raid_port_info);
}

void parse_and_print_sata_info(const uint8_t *info) {
    struct jmraid_sata_info sata_info;
    parse_jmraid_sata_info(info, &sata_info);
    print_sata_info(&sata_info);
}

void parse_and_print_sata_port_info(const uint8_t *info) {
    struct jmraid_sata_port_info sata_port_info;
    parse_jmraid_sata_port_info(info, &sata_port_info);
    print_sata_port_info(&sata_port_info);
}

int main(int argc, char * argv[])
{
    int sg_fd, k;
    uint8_t saveBuf[SECTORSIZE];
    uint8_t probeBuf[SECTORSIZE];
    uint8_t sense_buffer[32];
    uint32_t scrambled_cmd_code;

/*  printf("JMraidcon version x, Copyright (C) 2010 Werner Johansson\n" \
        "JMraidcon comes with ABSOLUTELY NO WARRANTY.\n" \
        "This is free software, and you are welcome\n" \
        "to redistribute it under certain conditions.\n\n" );
*/

    if (3 != argc) {
        printf("Usage : JMraidcon /dev/sd<X> <jms56x | jmb39x>\n");
        return 1;
    }

    if ((sg_fd = open(argv[1], O_RDWR)) < 0) {
        printf("Cannot open device");
        return 1;
    }

    if (strcmp(argv[2], "jms56x") == 0) {
        printf("Using JMS56x with sector 254 (0xfe)\n\n");
        scrambled_cmd_code = 0x197b0562;
    } else if (strcmp(argv[2], "jmb39x") == 0) {
        printf("Using JMB39x with sector 254 (0xfe)\n\n");
        scrambled_cmd_code = 0x197b0322;
    } else {
        printf("Controller not specified");
        return 1;
    }

    // Check if the opened device looks like a sg one.
    // Inspired by the sg_simple0 example
    if ((ioctl(sg_fd, SG_GET_VERSION_NUM, &k) < 0) || (k < 30000)) {
        printf("%s is not an sg device, or old sg driver\n", argv[1]);
        return 1;
    }

    // Setup the ioctl struct
    memset(&io_hdr, 0, sizeof(sg_io_hdr_t));
    io_hdr.interface_id = 'S';
    io_hdr.cmd_len = sizeof(rwCmdBlk);
    io_hdr.mx_sb_len = sizeof(sense_buffer);
    io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
    io_hdr.dxfer_len = SECTORSIZE;
    io_hdr.dxferp = saveBuf;
    io_hdr.cmdp = rwCmdBlk;
    io_hdr.sbp = sense_buffer;
    io_hdr.timeout = 3000;

    // Add more error handling like this later
    if( ioctl( sg_fd, SG_IO, &io_hdr ) < 0 ) {
        printf("ioctl SG_IO failed");
        return 1;
    }

    // Generate and send the initial "wakeup" data
    // No idea what the second dword represents at this point
    // Note that these (and all other writes) should be directed to an unused sector!!
    memset( probeBuf, 0, SECTORSIZE );

    // For wide access
    uint32_t* probeBuf32 = (uint32_t*)probeBuf;

    // Populate with the static data
    probeBuf32[0 >> 2] = __cpu_to_le32( JM_RAID_WAKEUP_CMD );
    probeBuf32[0x1f8 >> 2] = __cpu_to_le32( 0x10eca1db );
    for( uint32_t i=0x10; i<0x1f8; i++ ) {
        probeBuf[i] = i&0xff;
    }

    io_hdr.dxfer_direction = SG_DXFER_TO_DEV;
    rwCmdBlk[0] = WRITE_CMD;
    io_hdr.dxferp = probeBuf;

    // The only value (except the CRC at the end) that changes between the 4 wakeup sectors
    probeBuf32[4 >> 2] = __cpu_to_le32( 0x3c75a80b );
    uint32_t myCRC = JM_CRC( probeBuf32, 0x1fc >> 2 );
    probeBuf32[0x1fc >> 2] = __cpu_to_le32( myCRC );
    ioctl(sg_fd, SG_IO, &io_hdr);

    probeBuf32[4 >> 2] = __cpu_to_le32( 0x0388e337 );
    myCRC = JM_CRC( probeBuf32, 0x1fc >> 2 );
    probeBuf32[0x1fc >> 2] = __cpu_to_le32( myCRC );
    ioctl(sg_fd, SG_IO, &io_hdr);

    probeBuf32[4 >> 2] = __cpu_to_le32( 0x689705f3 );
    myCRC = JM_CRC( probeBuf32, 0x1fc >> 2 );
    probeBuf32[0x1fc >> 2] = __cpu_to_le32( myCRC );
    ioctl(sg_fd, SG_IO, &io_hdr);

    probeBuf32[4 >> 2] = __cpu_to_le32( 0xe00c523a );
    myCRC = JM_CRC( probeBuf32, 0x1fc >> 2 );
    probeBuf32[0x1fc >> 2] = __cpu_to_le32( myCRC );
    ioctl(sg_fd, SG_IO, &io_hdr);

    // Initial probe complete, now send scrambled commands to the same sector


    //Get Chip Info
    process_cmd(sg_fd, scrambled_cmd_code, (uint8_t*)getchipinfo_probe, sizeof(getchipinfo_probe), 0xC, parse_and_print_jmraid_chip_info);
    print("\n");

/*
    //Get Raid Port Info
    const uint8_t *info2 = SENDCMD( sg_fd, scrambled_cmd_code, (uint8_t*)getraidportinfo_probe, sizeof(getraidportinfo_probe) );
    struct jmraid_raid_port_info raid_port_info;
    parse_jmraid_raid_port_info(info2, &raid_port_info);
    print_raid_port_info(&raid_port_info);
    print("\n");

    GETCHIPINFO( sg_fd, scrambled_cmd_code, (uint8_t*)getchipinfo_probe, sizeof(getchipinfo_probe) );
    print("\n");
*/

    process_cmd(sg_fd, scrambled_cmd_code, (uint8_t*)getraidportinfo_probe, sizeof(getraidportinfo_probe), 0x10-0x04, parse_and_print_raid_port_info);
    print("\n");

    process_cmd(sg_fd, scrambled_cmd_code, (uint8_t*)getsatainfo_probe, sizeof(getsatainfo_probe), 0x10-0x04, parse_and_print_sata_info);
    print("\n");
    print("SATA Port 0 information:\n");
    process_cmd(sg_fd, scrambled_cmd_code, (uint8_t*)getsataport0info_probe, sizeof(getsataport0info_probe), 0x10-0x04, parse_and_print_sata_port_info);
    print("\n");
    print("SATA Port 1 information:\n");
    process_cmd(sg_fd, scrambled_cmd_code, (uint8_t*)getsataport1info_probe, sizeof(getsataport1info_probe), 0x10-0x04, parse_and_print_sata_port_info);
    print("\n");

    /* work in progress by Elmar (2022-12-10) */
    const uint8_t *resultBuf1 = malloc(SECTORSIZE);
    const uint8_t *resultBuf2 = malloc(SECTORSIZE);
    {
        print("SMART Info Disk 0:\n");
        send_cmd(sg_fd, scrambled_cmd_code, (uint8_t*)disk0smartread1_probe, sizeof(disk0smartread1_probe), resultBuf1);
        send_cmd(sg_fd, scrambled_cmd_code, (uint8_t*)disk0smartread2_probe, sizeof(disk0smartread2_probe), resultBuf2);
        struct jmraid_disk_smart_info disk_smart_info;
        parse_jmraid_disk_smart_info(resultBuf1+0x10-0x04, resultBuf2+0x10-0x04, &disk_smart_info);
        print_disk_smart_info(&disk_smart_info);
        print("\n");
    }
    {
        print("SMART Info Disk 1:\n");
        send_cmd(sg_fd, scrambled_cmd_code, (uint8_t*)disk1smartread1_probe, sizeof(disk1smartread1_probe), resultBuf1);
        send_cmd(sg_fd, scrambled_cmd_code, (uint8_t*)disk1smartread2_probe, sizeof(disk1smartread2_probe), resultBuf2);
        struct jmraid_disk_smart_info disk_smart_info;
        parse_jmraid_disk_smart_info(resultBuf1+0x10-0x04, resultBuf2+0x10-0x04, &disk_smart_info);
        print_disk_smart_info(&disk_smart_info);
        print("\n");
    }
    free(resultBuf1);
    free(resultBuf2);


    // Restore the original data to the sector
    io_hdr.dxfer_direction = SG_DXFER_TO_DEV;
    rwCmdBlk[0] = WRITE_CMD;
    io_hdr.dxferp = saveBuf;
    ioctl(sg_fd, SG_IO, &io_hdr);

    close(sg_fd);
    return 0;
}
