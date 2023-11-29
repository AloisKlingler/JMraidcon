#ifndef _JMRAID_H_
#define _JMRAID_H_

struct jmraid_raid_port_info_member
{
	uint8_t ready;
	uint8_t lba48_support;
	uint8_t sata_page;
	uint8_t sata_port;
	uint32_t sata_base;
	uint64_t sata_size;
};

struct jmraid_chip_info
{
        char product_name[0x20 + 1];
        char manufacturer[0x20 + 1];
        uint32_t serial_number;
        uint8_t firmware_version[4];
};

struct jmraid_raid_port_info
{
	unsigned char model_name[0x28 + 1];
	unsigned char serial_number[0x14 + 1];
	uint8_t port_state;
	uint8_t level;
	uint64_t capacity;
	uint8_t state;
	uint8_t member_count;
	uint16_t rebuild_priority;
	uint16_t standby_timer;
	char password[0x08 + 1];
	uint64_t rebuild_progress;
	struct jmraid_raid_port_info_member member[5];
};

struct jmraid_disk_smart_info_attribute
{
        uint8_t id;
        uint16_t flags;
        uint8_t current_value;
        uint8_t worst_value;
        uint64_t raw_value;
        uint8_t threshold;
};

struct jmraid_disk_smart_info
{
        struct jmraid_disk_smart_info_attribute attribute[30];
};

struct jmraid_sata_info_item
{
        char model_name[0x28 + 1];
        char serial_number[0x14 + 1];
        uint64_t capacity;
        uint8_t port_type;
        uint8_t port_speed;
        uint8_t page_0_state;
        uint8_t page_0_raid_index;
        uint8_t page_0_raid_member_index;
        uint8_t port;
};

struct jmraid_sata_info
{
        struct jmraid_sata_info_item item[5];
};

struct jmraid_sata_port_info
{
        char model_name[0x28 + 1];
        char serial_number[0x14 + 1];
        char firmware_version[0x08 + 1];
        uint64_t capacity;
        uint64_t capacity_used;
        uint8_t port_type;
        uint8_t port;
        uint8_t page_0_state;
        uint8_t page_0_raid_index;
        uint8_t page_0_raid_member_index;
};


#endif
