// Attempt to dump DVRP flash and internal IPL (failure due to DMA source restriction)

#include <stdio.h>
#include <kernel.h>
#include <iopcontrol.h>
#include <iopheap.h>
#include <debug.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <stdlib.h>
#include <string.h>
#include <hdd-ioctl.h>
#include <libpwroff.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <libcdvd.h>
#include <io_common.h>

extern unsigned char DEV9_irx[];
extern unsigned int size_DEV9_irx;

extern unsigned char IOMANX_irx[];
extern unsigned int size_IOMANX_irx;

extern unsigned char FILEXIO_irx[];
extern unsigned int size_FILEXIO_irx;

extern unsigned char POWEROFF_irx[];
extern unsigned int size_POWEROFF_irx;

extern unsigned char ATAD_irx[];
extern unsigned int size_ATAD_irx;

extern unsigned char HDD_irx[];
extern unsigned int size_HDD_irx;

extern unsigned char DVRDRV_irx[];
extern unsigned int size_DVRDRV_irx;

extern unsigned char DVRFILE_irx[];
extern unsigned int size_DVRFILE_irx;

extern unsigned char USBD_irx[];
extern unsigned int size_USBD_irx;

extern unsigned char BDM_irx[];
extern unsigned int size_BDM_irx;

extern unsigned char BDMFS_FATFS_irx[];
extern unsigned int size_BDMFS_FATFS_irx;

extern unsigned char USBMASS_BD_irx[];
extern unsigned int size_USBMASS_BD_irx;

static void poweroffCallback(void *arg);

static const char *device_point = "hdd0:";

static void dump_info(void);

int main(int argc, char *argv[])
{
	//Reboot IOP
	SifInitRpc(0);
	while(!SifIopReset("", 0)){};
	while(!SifIopSync()){};

	//Initialize SIF services
	SifInitRpc(0);
	SifLoadFileInit();
	SifInitIopHeap();
	sbv_patch_enable_lmb();

	//Load modules
	SifExecModuleBuffer(DEV9_irx, size_DEV9_irx, 0, NULL, NULL);

	SifExecModuleBuffer(IOMANX_irx, size_IOMANX_irx, 0, NULL, NULL);
	SifExecModuleBuffer(FILEXIO_irx, size_FILEXIO_irx, 0, NULL, NULL);


	fileXioInit();
	SifExecModuleBuffer(POWEROFF_irx, size_POWEROFF_irx, 0, NULL, NULL);
	poweroffInit();
	poweroffSetCallback(&poweroffCallback, NULL);

	init_scr();

	scr_printf("Initing USB:\n");
	SifExecModuleBuffer(USBD_irx, size_USBD_irx, 0, NULL, NULL);
	SifExecModuleBuffer(BDM_irx, size_BDM_irx, 0, NULL, NULL);
	SifExecModuleBuffer(BDMFS_FATFS_irx, size_BDMFS_FATFS_irx, 0, NULL, NULL);
	SifExecModuleBuffer(USBMASS_BD_irx, size_USBMASS_BD_irx, 0, NULL, NULL);
	{
		iox_stat_t chk_stat;
		while (fileXioGetStat("mass0:/", &chk_stat) < 0) {sleep(1);};
	}

	scr_printf("Initing HDD:\n");
	//Load modules
	SifExecModuleBuffer(ATAD_irx, size_ATAD_irx, 0, NULL, NULL);
	SifExecModuleBuffer(HDD_irx, size_HDD_irx, 0, NULL, NULL);

	scr_printf("Initing DVR:\n");
#if 0
	{
		u8 out[16];
		u8 in[16];

		in[0] = 0;
		// sceCdXDVRPReset
		sceCdApplySCmd(0x33, in, 1, out);
	}
#endif
	{
		u8 out[16];
		u8 in[16];

		in[0] = 0;
		// sceCdNoticeGameStart
		sceCdApplySCmd(0x29, in, 1, out);
	}
	//Load modules
	SifExecModuleBuffer(DVRDRV_irx, size_DVRDRV_irx, 0, NULL, NULL);
	SifExecModuleBuffer(DVRFILE_irx, size_DVRFILE_irx, 0, NULL, NULL);
    if (!fileXioDevctl("dvr_hdd0:", HDIOC_STATUS, NULL, 0, NULL, 0) && (fileXioDevctl("dvr_hdd0:", HDIOC_ISLBA48, NULL, 0, NULL, 0) == 1))
    {
    	scr_printf("DVR is available!\n");
    	device_point = "dvr_hdd0:";
    }

	//At this point, network support has been initialized and the PS2 can be pinged.
	scr_printf("Everything inited!\n");
	dump_info();
	SleepThread();


	//Deinitialize SIF services
	SifExitRpc();

	return 0;
}

#define mips_memcpy memcpy
#define mips_memset memset

#if 0
static inline int CreateMutex(int state)
{
    ee_sema_t sema;
    sema.option  = 0;
    sema.init_count = state;
    sema.max_count     = 1;
    return CreateSema(&sema);
}
#endif

typedef struct _ata_devinfo
{
    /** Total number of user sectors.  */
    u32 total_sectors;
} ata_devinfo_t;

static ata_devinfo_t ata_devinfo;

int ata_device_flush_cache(int device)
{
	// hdd: HDIOC_FLUSH nonzero -> -EIO, zero -> zero
	return (fileXioDevctl(device_point, HDIOC_FLUSH, NULL, 0, NULL, 0) < 0) ? 1 : 0;
}

void dev9Shutdown(void)
{
	while (fileXioDevctl("dev9x:", DDIOC_OFF, NULL, 0, NULL, 0) < 0);
}

void PoweroffShutdown()
{
	poweroffShutdown();
}

ata_devinfo_t *ata_get_devinfo(int device)
{
	// ata_devinfo.total_sectors = //fileXioDevctl(device_point, HDIOC_TOTALSECTOR, NULL, 0, NULL, 0);
	ata_devinfo.total_sectors = fileXioDevctl(device_point, HDIOC_GETMAXLBA48, NULL, 0, NULL, 0);
	return &ata_devinfo;
}

#define ATA_DIR_READ  0
#define ATA_DIR_WRITE 1

static uint8_t IOBuffer[2048];

static inline u32 bswap32(u32 val)
{
#if 0
    return __builtin_bswap32(val);
#else
    return (val << 24) + ((val & 0xFF00) << 8) + ((val >> 8) & 0xFF00) + ((val >> 24) & 0xFF);
#endif
}

int hddReadSectors(u32 lba, u32 nsectors, void *buf)
{
    hddAtaTransfer_t *args = (hddAtaTransfer_t *)IOBuffer;
    u32 lba_offset;

    // filexio is limited to 2048 (4 sectors) bytes both arg and buf buffers.
    // dvrfile is limited to 32768 bytes (64 sectors) both arg and buf buffers.
    for (lba_offset = 0; lba_offset < nsectors; lba_offset += args->size)
    {
    	u32 xlba;
    	u32 xsize;

        xlba = lba + lba_offset;
        xsize = (nsectors - lba_offset) > 4 ? 4 : (nsectors - lba_offset);

        // TODO don't bswap32 for normal HDD
        args->lba = bswap32(xlba);
        args->size = bswap32(xsize);

        if (fileXioDevctl(device_point, HDIOC_READSECTOR, args, sizeof(hddAtaTransfer_t), ((u8 *)buf) + (lba_offset * 512), xsize * 512) != 0)
            return -1;
    }

    return 0;
}

int hddWriteSectors(u32 lba, u32 nsectors, const void *buf)
{
    static u8 WriteBuffer[3 * 512 + sizeof(hddAtaTransfer_t)]; // Has to be a different buffer from IOBuffer (input can be in IOBuffer).
    hddAtaTransfer_t *args = (hddAtaTransfer_t *)WriteBuffer;
    u32 lba_offset;

    // filexio is limited to 2048 (4 sectors) bytes both arg and buf buffers.
    // dvrfile is limited to 32768 bytes (64 sectors) both arg and buf buffers.
    for (lba_offset = 0; lba_offset < nsectors; lba_offset += args->size)
    {
    	u32 xlba;
    	u32 xsize;
        xlba = lba + lba_offset;
        xsize = (nsectors - lba_offset) > 3 ? 3 : (nsectors - lba_offset);
        memcpy(args->data, ((u8 *)buf) + (lba_offset * 512), xsize * 512);

        // TODO don't bswap32 for normal HDD
        args->lba = bswap32(xlba);
        args->size = bswap32(xsize);
        
        if (fileXioDevctl(device_point, HDIOC_WRITESECTOR, args, sizeof(hddAtaTransfer_t) + (xsize * 512), NULL, 0) != 0)
            return -1;
    }

    return 0;
}

__attribute__ ((noinline))
int ata_device_sector_io(int device, void *buf, u32 lba, u32 nsectors, int dir)
{
	if (device != 0)
	{
		return -1;
	}
	switch (dir)
	{
	case ATA_DIR_READ:
		return hddReadSectors(lba, nsectors, buf);
	case ATA_DIR_WRITE:
		return hddWriteSectors(lba, nsectors, buf);
	default:
		return -1;
	}
}

static void poweroffCallback(void *arg)
{
	scr_printf("Powering off\n");

    scr_printf("dev9x off\n");
    while (fileXioDevctl("dev9x:", DDIOC_OFF, NULL, 0, NULL, 0) < 0);

    scr_printf("Shut down!!!\n");
    poweroffShutdown();
}

#define u_long u32
#define u_short u16
#define u_char u8

#define u_int32_t u_long
#define u_int16_t u16
#define u_int8_t u8

/* Various PS2 partition constants */
#define PS2_PARTITION_MAGIC "APA" /* "APA\0" */
#define PS2_PART_IDMAX      32
#define PS2_PART_NAMEMAX    128
#define PS2_PART_MAXSUB     64     /* Maximum # of sub-partitions */
#define PS2_PART_FLAG_SUB   0x0001 /* Is partition a sub-partition? */
#define PS2_MBR_VERSION     2      /* Current MBR version */
#define PS2_MBR_MAGIC       "Sony Computer Entertainment Inc."

/* Partition types */
#define PS2_MBR_PARTITION   0x0001
#define PS2_SWAP_PARTITION  0x0082
#define PS2_LINUX_PARTITION 0x0083
#define PS2_GAME_PARTITION  0x0100
#define PS2_HDL_PARTITION   0x1337

/* Date/time descriptor used in on-disk partition header */
typedef struct ps2fs_datetime_type
{
    u_int8_t unused;
    u_int8_t sec;
    u_int8_t min;
    u_int8_t hour;
    u_int8_t day;
    u_int8_t month;
    u_int16_t year;
} ps2fs_datetime_t;

/* On-disk partition header for a partition */
typedef struct ps2_partition_header_type
{
    u_int32_t checksum; /* Sum of all 256 words, assuming checksum==0 */
    u_int8_t magic[4];  /* PS2_PARTITION_MAGIC */
    u_int32_t next;     /* Sector address of next partition */
    u_int32_t prev;     /* Sector address of previous partition */
    char id[PS2_PART_IDMAX];
    char unknown1[16];
    u_int32_t start;  /* Sector address of this partition */
    u_int32_t length; /* Sector count */
    u_int16_t type;
    u_int16_t flags; /* PS2_PART_FLAG_* */
    u_int32_t nsub;  /* No. of sub-partitions (stored in main partition) */
    ps2fs_datetime_t created;
    u_int32_t main;   /* For sub-partitions, main partition sector address */
    u_int32_t number; /* For sub-partitions, sub-partition number */
    u_int32_t modver;
    u_int32_t pading1[7];
    char name[PS2_PART_NAMEMAX];
    struct
    {
        char magic[32]; /* Copyright message in MBR */
        u_int32_t version;
        u_int32_t nsector;
        ps2fs_datetime_t created; /* Same as for the partition, it seems*/
        u_int32_t data_start;     /* Some sort of MBR data; position in sectors*/
        u_int32_t data_len;       /* Length also in sectors */

        char unknown2[72];

        /* DMS-/ToxicOS-specific */
        char dms_boot_magic[32];
        u_int32_t boot_elf_installed;
        u_int32_t boot_elf_lba;
        u_int32_t boot_elf_byte_size;
        u_int32_t boot_elf_checksum;
        u_int32_t boot_elf_virtual_addr;
        u_int32_t boot_elf_start_addr;
        char unknown3[72 - 12];
        char toxic_magic[8];
        u_int32_t toxic_flags;
    } mbr;
    struct
    {                     /* Sub-partition data */
        u_int32_t start;  /* Sector address */
        u_int32_t length; /* Sector count */
    } subs[PS2_PART_MAXSUB];
} ps2_partition_header_t;

static const char MAP_AVAIL = '.';
static const char MAP_MAIN = 'M';
static const char MAP_SUB = 's';
static const char MAP_COLL = 'x';
#if 0
static const char MAP_ALLOC = '*';
#endif

typedef struct apa_partition_type
{
    int existing;
    int modified;
    int linked;
    ps2_partition_header_t header;
} apa_partition_t;


typedef struct apa_partition_table_type
{
    u_long device_size_in_mb;
    u_long total_chunks;
    u_long allocated_chunks;
    u_long free_chunks;

    char *chunks_map;

    /* existing partitions */
    u_long part_alloc_;
    u_long part_count;
    apa_partition_t *parts;
} apa_partition_table_t;

void apa_ptable_free(apa_partition_table_t *table);

u_long apa_partition_checksum(const ps2_partition_header_t *part);

u_long get_u32(const void *buffer);
void set_u32(void *buffer, u_long val);

u_short get_u16(const void *buffer);
void set_u16(void *buffer, u_short val);

u_long get_u32(const void *buffer)
{
    const u_char *p = buffer;
    return ((((u_long)p[3]) << 24) |
            (((u_long)p[2]) << 16) |
            (((u_long)p[1]) << 8) |
            (((u_long)p[0]) << 0));
}
//------------------------------
// endfunc get_u32
//--------------------------------------------------------------
void set_u32(void *buffer, u_long val)
{
    u_char *p = buffer;
    p[3] = (u_char)(val >> 24);
    p[2] = (u_char)(val >> 16);
    p[1] = (u_char)(val >> 8);
    p[0] = (u_char)(val >> 0);
}
//------------------------------
// endfunc set_u32
//--------------------------------------------------------------
u_short get_u16(const void *buffer)
{
    const u_char *p = buffer;
    return ((((u_short)p[1]) << 8) |
            (((u_short)p[0]) << 0));
}
//------------------------------
// endfunc get_u16
//--------------------------------------------------------------
void set_u16(void *buffer, u_short val)
{
    u_char *p = buffer;
    p[1] = (u_char)(val >> 8);
    p[0] = (u_char)(val >> 0);
}
//------------------------------
// endfunc set_u16
//--------------------------------------------------------------
// End of file: apa.c
//--------------------------------------------------------------

#define _MB *(1024 * 1024) /* really ugly :-) */

// Remove this line, and uncomment the next line, to reactivate 'apa_check'
// static int apa_check(const apa_partition_table_t *table);

//--------------------------------------------------------------
u_long apa_partition_checksum(const ps2_partition_header_t *part)
{
    const u_long *p = (const u_long *)part;
    register u_long i;
    u_long sum = 0;
    for (i = 1; i < 256; ++i)
        sum += get_u32(p + i);
    return sum;
}
//------------------------------
// endfunc apa_partition_checksum
//--------------------------------------------------------------
static apa_partition_table_t *apa_ptable_alloc(void)
{
    apa_partition_table_t *table = malloc(sizeof(apa_partition_table_t));
    if (table != NULL)
        memset(table, 0, sizeof(apa_partition_table_t));
    return table;
}
//------------------------------
// endfunc apa_ptable_alloc
//--------------------------------------------------------------
void apa_ptable_free(apa_partition_table_t *table)
{
    if (table != NULL) {
        if (table->chunks_map != NULL)
            free(table->chunks_map);
        if (table->parts != NULL)
            free(table->parts);
        free(table);
    }
}
//------------------------------
// endfunc apa_ptable_free
//--------------------------------------------------------------
static int apa_part_add(apa_partition_table_t *table, const ps2_partition_header_t *part, int existing, int linked)
{
    if (table->part_count == table->part_alloc_) { /* grow buffer */
        u_long bytes = (table->part_alloc_ + 16) * sizeof(apa_partition_t);
        apa_partition_t *tmp = malloc(bytes);
        if (tmp != NULL) {
            memset(tmp, 0, bytes);
            if (table->parts != NULL) /* copy existing */
                memcpy(tmp, table->parts, table->part_count * sizeof(apa_partition_t));
            free(table->parts);
            table->parts = tmp;
            table->part_alloc_ += 16;
        } else
            return -2;
    }

    memcpy(&table->parts[table->part_count].header, part, sizeof(ps2_partition_header_t));
    table->parts[table->part_count].existing = existing;
    table->parts[table->part_count].modified = !existing;
    table->parts[table->part_count].linked = linked;
    ++table->part_count;

    return 0;
}
//------------------------------
// endfunc apa_part_add
//--------------------------------------------------------------
// Remove this line and a similar one below to reactivate 'apa_setup_statistics'
//------------------------------
// endfunc apa_setup_statistics
//--------------------------------------------------------------
int apa_ptable_read_ex(apa_partition_table_t **table, int offset)
{
    u_long size_in_sectors;
	//fileXioDevctl(device_point, HDIOC_TOTALSECTOR, NULL, 0, NULL, 0);
	size_in_sectors = fileXioDevctl(device_point, HDIOC_GETMAXLBA48, NULL, 0, NULL, 0) + 1;
    int result = 0;
    *table = apa_ptable_alloc();
    if (*table != NULL) {
        u_long sector = offset;
        do {
            ps2_partition_header_t part;
            result = ata_device_sector_io(0, &part, sector, sizeof(part) / 512, ATA_DIR_READ);
            if (result == 0) {
                if (
                    get_u32(&part.checksum) == apa_partition_checksum(&part) &&
                    memcmp(part.magic, PS2_PARTITION_MAGIC, 4) == 0) {

                    if (get_u32(&part.start) < size_in_sectors &&
                        get_u32(&part.start) + get_u32(&part.length) < size_in_sectors) {
                        result = apa_part_add(*table, &part, 1, 1);
                        if (result == 0)
                            sector = get_u32(&part.next);
                    } else {        /* partition behind end-of-HDD */
                        result = 7; /* data behind end-of-HDD */
                        break;
                    }
                } else
                    result = 1;
            }
            /* TODO: check whether next partition is not loaded already --
             * do not deadlock; that is a quick-and-dirty hack */
            if ((*table)->part_count > 10000)
                result = 8;
        } while (result == 0 && sector != 0);

        if (result == 0) {
            (*table)->device_size_in_mb = size_in_sectors / 2;
            // NB: uncommenting the next lines requires changes elsewhere too
            // result = apa_setup_statistics (*table);
            // if (result == 0)
            // result = apa_check (*table);
        }

        if (result != 0) {
            result = 20000 + (*table)->part_count;
            apa_ptable_free(*table);
        }
    } else
        result = -2;
    return result;
}
//------------------------------
// endfunc apa_ptable_read_ex
//--------------------------------------------------------------
// Remove this line and a similar one above to reactivate 'apa_check'
//------------------------------
// endfunc apa_check
//--------------------------------------------------------------

static void
apa_setup_statistics(/*@special@*/ apa_partition_table_t *slice)
/*@uses slice->size_in_mb, slice->part_count, slice->parts@*/
/*@sets slice->total_chunks, slice->allocated_chunks,
       slice->free_chunks, slice->chunks_map, *slice->chunks_map@*/
{

    char *map;

    slice->total_chunks = slice->device_size_in_mb / 128;
    map = malloc(slice->total_chunks * sizeof(char));
    if (map != NULL) {
        u_int32_t i;
        *map = MAP_AVAIL;
        for (i = 0; i < slice->total_chunks; ++i)
            map[i] = MAP_AVAIL;

        /* build occupided/available space map */
        slice->allocated_chunks = 0;
        slice->free_chunks = slice->total_chunks;
        for (i = 0; i < slice->part_count; ++i) {
            const ps2_partition_header_t *part = &slice->parts[i].header;
            u_int32_t part_no = get_u32(&part->start) / ((128 _MB) / 512);
            u_int32_t num_parts = get_u32(&part->length) / ((128 _MB) / 512);

            /* "alloc" num_parts starting at part_no */
            while (num_parts) {
                if (map[part_no] == MAP_AVAIL)
                    map[part_no] = (get_u32(&part->main) == 0 ?
                                        MAP_MAIN :
                                        MAP_SUB);
                else
                    map[part_no] = MAP_COLL; /* collision */
                ++part_no;
                --num_parts;
                ++slice->allocated_chunks;
                --slice->free_chunks;
            }
        }

        if (slice->chunks_map != NULL)
            free(slice->chunks_map);
        slice->chunks_map = map;

    }
}

static void
show_apa_slice2(const apa_partition_table_t *slice)
{
    u_int32_t i;

    if (slice->parts == NULL)
        return;
    scr_printf("type   start     #parts size name\n");
    for (i = 0; i < slice->part_count; ++i) {
        const ps2_partition_header_t *part = &slice->parts[i].header;
        if (get_u32(&part->main) == 0) {
            u_int32_t j, count = get_u32(&part->nsub);
            u_int32_t tot_len = get_u32(&part->length);
            for (j = 0; j < count; ++j)
                tot_len += get_u32(&part->subs[j].length);
            scr_printf("0x%04x %06lx00%c%c %2lu %5luMB %s\n",
                    (unsigned int)get_u16(&part->type),
                    (unsigned long)get_u32(&part->start) >> 8,
                    slice->parts[i].existing != 0 ? '.' : '*',
                    slice->parts[i].modified != 0 ? '*' : ':',
                    (unsigned long)count + 1, /* main partition counts, too */
                    (unsigned long)tot_len / 2048,
                    part->id);
        }
    }

    scr_printf("Total slice size: %uMB, used: %uMB, available: %uMB\n",
            (unsigned int)slice->device_size_in_mb,
            (unsigned int)(slice->allocated_chunks * 128),
            (unsigned int)(slice->free_chunks * 128));
}

u32 cdvdman_crc32_for_byte(u32 r) {
    for(int j = 0; j < 8; ++j)
        r = (r & 1? 0: (u32)0xEDB88320L) ^ r >> 1;
    return r ^ (u32)0xFF000000L;
}

#if 0
static u32 table[0x100];
void cdvdman_crc32(const void *data, u32 n_bytes, u32* crc) {
    for(u32 i = 0; i < n_bytes; ++i)
        *crc = table[(u8)*crc ^ ((u8*)data)[i]] ^ *crc >> 8;
}
#endif


__attribute__ ((noinline))
static void compare_known(u8 *buf, u32 cnt, u32 extend_partoffs)
{
	u8 buf2[512];
	int i;
	int cmp2_count;

	cmp2_count = 0;
	if (extend_partoffs)
	{
		for (i = 0; i < cnt; i += 1)
		{
			ata_device_sector_io(0, buf2, extend_partoffs + i, sizeof(buf2) / 512, ATA_DIR_READ);
    		if (!memcmp(buf2, buf, 512))
    		{
    			cmp2_count += 1;
    		}
		}
	}
	scr_printf("cmp2 Count: %d \n", cmp2_count);
}


static void dump_info(void)
{
    apa_partition_table_t *ptable;
    int result;
#if 0
    for(u32 i = 0; i < 0x100; ++i)
        table[i] = cdvdman_crc32_for_byte(i);
#endif
    result = apa_ptable_read_ex(&ptable, 0);
    if (result == 0) {
    	apa_setup_statistics(ptable);
    	show_apa_slice2(ptable);
        // u_long i, count = 0;
        // void *tmp;
        // for (i = 0; i < ptable->part_count; ++i)
        //     count += (get_u16(&ptable->parts[i].header.flags) == 0x00 &&
        //               get_u16(&ptable->parts[i].header.type) == 0x1337);

#if 0
        tmp = malloc(sizeof(hdl_game_info_t) * count);
        if (tmp != NULL) {
            memset(tmp, 0, sizeof(hdl_game_info_t) * count);
            *glist = malloc(sizeof(hdl_games_list_t));
            if (*glist != NULL) {
                u_long index = 0;
                memset(*glist, 0, sizeof(hdl_games_list_t));
                (*glist)->count = count;
                (*glist)->games = tmp;
                (*glist)->total_chunks = ptable->total_chunks;
                (*glist)->free_chunks = ptable->free_chunks;
                for (i = 0; result == 0 && i < ptable->part_count; ++i) {
                    const ps2_partition_header_t *part = &ptable->parts[i].header;
                    if (get_u16(&part->flags) == 0x00 && get_u16(&part->type) == 0x1337)
                        result = hdl_ginfo_read(hio, part, (*glist)->games + index++);
                }
                if (result != 0)
                    free(*glist);
            } else
                result = -2;
            if (result != 0)
                free(tmp);
        } else
            result = -2;
#endif

        apa_ptable_free(ptable);
    }
    result = apa_ptable_read_ex(&ptable, fileXioDevctl(device_point, HDIOC_TOTALSECTOR, NULL, 0, NULL, 0));
    if (result == 0) {
    	apa_setup_statistics(ptable);
    	show_apa_slice2(ptable);

        apa_ptable_free(ptable);
    }

#if 0
    int zero_count;
    int nonzero_count;
#endif
    int cmp_count;
    u32 extend_partoffs;
    u32 extend_partsize;
#if 0
    u32 crc;
#endif
    int fd1;

    {
    	extend_partoffs = 0;
    	extend_partsize = 0;
	    if (ptable->parts != NULL)
	    {
	    	u32 i;
		    for (i = 0; i < ptable->part_count; ++i)
		    {
		        const ps2_partition_header_t *part = &ptable->parts[i].header;
		        if (get_u32(&part->main) == 0 && !strcmp(part->id, "__extend"))
		        {
		        	u32 count = get_u32(&part->nsub);
		        	u32 j;

		        	extend_partoffs = get_u32(&part->start);
		            u_int32_t tot_len = get_u32(&part->length);
		            for (j = 0; j < count; ++j)
		                tot_len += get_u32(&part->subs[j].length);
		            // extend_partsize = (tot_len >> 4) + 8;
		            extend_partsize = tot_len;
		            break;
		        }
		    }
	    }
    }
    scr_printf("Extend info: %08x %08x\n", extend_partoffs, extend_partsize);
    
#if 0
    zero_count = 0;
    nonzero_count = 0;
#endif
    cmp_count = 0;
#if 0
    crc = 0;
#endif
    u8 buf[512];
    int cnt;
    cnt = 0;
    fd1 = fileXioOpen("dvr_hdd0:__extend", FIO_O_RDWR);
    if (fd1 >= 0)
    {
    	int i;
    	u8 *sbuf;
#if 0
    	u8 fbuf[512];

    	while (fileXioRead(fd1, buf, sizeof(buf)) == sizeof(buf))
    	{
#if 0
    		int i;
    		int is_all_zero;
    		is_all_zero = 1;
#endif
    		if (!cnt)
    		{
    			memcpy(fbuf, buf, sizeof(buf));
    		}
#if 0
    		for (i = 0; i < sizeof(buf); i += 1)
    		{
    			if (buf[i] != 0)
    			{
    				is_all_zero = 0;
    				break;
    			}
    		}
#endif
#if 0
    		if (is_all_zero)
    		{
    			zero_count += 1;
    		}
    		else
    		{
    			nonzero_count += 1;
    		}
#endif
    		if (!memcmp(fbuf, buf, 512))
    		{
    			cmp_count += 1;
    		}
    		cnt += 1;
    		// cdvdman_crc32(buf, sizeof(buf), &crc);
    		// scr_printf("CCRC: 0x%08x\n", crc);
    	}
#endif
    	int szsbuf;
    	szsbuf = 4096;
    	if (extend_partoffs)
    	{
	    	sbuf = malloc(szsbuf);
	    	if (sbuf)
	    	{
	    		memset(sbuf, 0, szsbuf);
			    for (i = 0; i < szsbuf; i += 512)
			    {
			    	ata_device_sector_io(0, sbuf + i, extend_partoffs + 0x2000 + (i / 512), 1, ATA_DIR_READ);
			    }
			    hddIoctl2Transfer_t xferParams;

			    xferParams.sub    = bswap32(0);
			    xferParams.sector = bswap32(0x2000);
			    // xferParams.size   = bswap32(szsbuf / 512); // 4096 bytes
			    xferParams.size = bswap32(1);
			    xferParams.mode   = bswap32(APA_IO_MODE_WRITE);
			    // xferParams.buffer = (void *)bswap32(0x00010000); // fail (will hang longer!)
			    // xferParams.buffer = (void *)bswap32(0x0003F000); // fail
			    xferParams.buffer = (void *)bswap32(0x00040000);
			    // xferParams.buffer = (void *)bswap32(0x000FF000); // fail
			    // xferParams.buffer = (void *)bswap32(0x00100000); // fail
			    // xferParams.buffer = (void *)bswap32(0x10000000); // OK
			    fileXioIoctl2(fd1, HIOCTRANSFER, &xferParams, sizeof(xferParams), NULL, 0);
			    {
			    	u8 xbuf[4096];
			    	memset(xbuf, 0, sizeof(xbuf));
			    	ata_device_sector_io(0, xbuf, extend_partoffs + 0x2000, sizeof(xbuf) / 512, ATA_DIR_READ);
			    	int fd2;
			    	fd2 = fileXioOpen("mass0:/00dvrp.bin", FIO_O_WRONLY | FIO_O_CREAT);
			    	if (fd2 >= 0)
			    	{
			    		fileXioWrite(fd2, xbuf, sizeof(xbuf));
			    		fileXioClose(fd2);
			    		scr_printf("disk write success\n");
			    	}
			    }
			    for (i = 0; i < szsbuf; i += 512)
			    {
			    	ata_device_sector_io(0, sbuf + i, extend_partoffs + 0x2000 + (i / 512), 1, ATA_DIR_WRITE);
			    }
			    free(sbuf);
			    scr_printf("restored success\n");
	    	}

    	}

    	fileXioClose(fd1);
    }
    else
    {
    	scr_printf("open err\n");
    }

    // scr_printf("Count: %d %d %d\n", zero_count, nonzero_count, cmp_count);
    scr_printf("Count: %d %d\n", cnt, cmp_count);
#if 0
    scr_printf("CRC: 0x%08x\n", crc);
#endif
    // compare_known(buf, cnt, extend_partoffs);
}

// typedef struct
// {
//     /** main(0)/subs(1+) to read/write */
//     u32 sub; // +0 [0]
//     u32 sector; // +4 [1]
//     /** in sectors */
//     u32 size; // +8 [2]
//     /** ATAD_MODE_READ/ATAD_MODE_WRITE..... */
//     u32 mode; // +12 [3]
//     void *buffer; // +16 [4]
// } hddIoctl2Transfer_t;

//     if (blkIoDmaTransfer(device, arg->buffer 4, fileSlot->parts[arg->sub].start + arg->sector 1, arg->size 2, arg->mode 3))