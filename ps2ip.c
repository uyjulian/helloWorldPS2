// dvr_hdd0: raw access by hdl-dump UDP protocol
// This is a mashup of OPL, hdl-dump, and wLaunchELF code
// NOTE: System hangs after 10GB transferred so (requiring force restart each time).
// NOTE: Thus not working in its current state

#include <stdio.h>
#include <kernel.h>
#include <iopcontrol.h>
#include <iopheap.h>
#include <debug.h>
#include <netman.h>
#include <ps2ip.h>
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

extern unsigned char DEV9_irx[];
extern unsigned int size_DEV9_irx;

extern unsigned char SMAP_irx[];
extern unsigned int size_SMAP_irx;

extern unsigned char NETMAN_irx[];
extern unsigned int size_NETMAN_irx;

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

static int ethApplyNetIFConfig(int mode)
{
	int result;
	//By default, auto-negotiation is used.
	static int CurrentMode = NETMAN_NETIF_ETH_LINK_MODE_AUTO;

	if(CurrentMode != mode)
	{	//Change the setting, only if different.
		if((result = NetManSetLinkMode(mode)) == 0)
			CurrentMode = mode;
	}else
		result = 0;

	return result;
}

static void EthStatusCheckCb(s32 alarm_id, u16 time, void *common)
{
	iWakeupThread(*(int*)common);
}

static int WaitValidNetState(int (*checkingFunction)(void))
{
	int ThreadID, retry_cycles;

	// Wait for a valid network status;
	ThreadID = GetThreadId();
	for(retry_cycles = 0; checkingFunction() == 0; retry_cycles++)
	{	//Sleep for 1000ms.
		SetAlarm(1000 * 16, &EthStatusCheckCb, &ThreadID);
		SleepThread();

		if(retry_cycles >= 10)	//10s = 10*1000ms
			return -1;
	}

	return 0;
}

static int ethGetNetIFLinkStatus(void)
{
	return(NetManIoctl(NETMAN_NETIF_IOCTL_GET_LINK_STATUS, NULL, 0, NULL, 0) == NETMAN_NETIF_ETH_LINK_STATE_UP);
}

static int ethWaitValidNetIFLinkState(void)
{
	return WaitValidNetState(&ethGetNetIFLinkStatus);
}

static int ethGetDHCPStatus(void)
{
	t_ip_info ip_info;
	int result;

	if ((result = ps2ip_getconfig("sm0", &ip_info)) >= 0)
	{	//Check for a successful state if DHCP is enabled.
		if (ip_info.dhcp_enabled)
			result = (ip_info.dhcp_status == DHCP_STATE_BOUND || (ip_info.dhcp_status == DHCP_STATE_OFF));
		else
			result = -1;
	}

	return result;
}

static int ethWaitValidDHCPState(void)
{
	return WaitValidNetState(&ethGetDHCPStatus);
}

static int ethApplyIPConfig(int use_dhcp, const struct ip4_addr *ip, const struct ip4_addr *netmask, const struct ip4_addr *gateway, const struct ip4_addr *dns)
{
	t_ip_info ip_info;
	int result;

	//SMAP is registered as the "sm0" device to the TCP/IP stack.
	if ((result = ps2ip_getconfig("sm0", &ip_info)) >= 0)
	{
		const ip_addr_t *dns_curr;

		//Obtain the current DNS server settings.
		dns_curr = dns_getserver(0);

		//Check if it's the same. Otherwise, apply the new configuration.
		if ((use_dhcp != ip_info.dhcp_enabled)
		    ||	(!use_dhcp &&
			 (!ip_addr_cmp(ip, (struct ip4_addr *)&ip_info.ipaddr) ||
			 !ip_addr_cmp(netmask, (struct ip4_addr *)&ip_info.netmask) ||
			 !ip_addr_cmp(gateway, (struct ip4_addr *)&ip_info.gw) ||
			 !ip_addr_cmp(dns, dns_curr))))
		{
			if (use_dhcp)
			{
				ip_info.dhcp_enabled = 1;
			}
			else
			{	//Copy over new settings if DHCP is not used.
				ip_addr_set((struct ip4_addr *)&ip_info.ipaddr, ip);
				ip_addr_set((struct ip4_addr *)&ip_info.netmask, netmask);
				ip_addr_set((struct ip4_addr *)&ip_info.gw, gateway);

				ip_info.dhcp_enabled = 0;
			}

			//Update settings.
			result = ps2ip_setconfig(&ip_info);
			if (!use_dhcp)
				dns_setserver(0, dns);
		}
		else
			result = 0;
	}

	return result;
}

static void ethPrintIPConfig(void)
{
	t_ip_info ip_info;
	u8 ip_address[4], netmask[4], gateway[4], dns[4];

	//SMAP is registered as the "sm0" device to the TCP/IP stack.
	if (ps2ip_getconfig("sm0", &ip_info) >= 0)
	{
		const ip_addr_t *dns_curr;

		//Obtain the current DNS server settings.
		dns_curr = dns_getserver(0);

		ip_address[0] = ip4_addr1((struct ip4_addr *)&ip_info.ipaddr);
		ip_address[1] = ip4_addr2((struct ip4_addr *)&ip_info.ipaddr);
		ip_address[2] = ip4_addr3((struct ip4_addr *)&ip_info.ipaddr);
		ip_address[3] = ip4_addr4((struct ip4_addr *)&ip_info.ipaddr);

		netmask[0] = ip4_addr1((struct ip4_addr *)&ip_info.netmask);
		netmask[1] = ip4_addr2((struct ip4_addr *)&ip_info.netmask);
		netmask[2] = ip4_addr3((struct ip4_addr *)&ip_info.netmask);
		netmask[3] = ip4_addr4((struct ip4_addr *)&ip_info.netmask);

		gateway[0] = ip4_addr1((struct ip4_addr *)&ip_info.gw);
		gateway[1] = ip4_addr2((struct ip4_addr *)&ip_info.gw);
		gateway[2] = ip4_addr3((struct ip4_addr *)&ip_info.gw);
		gateway[3] = ip4_addr4((struct ip4_addr *)&ip_info.gw);

		dns[0] = ip4_addr1(dns_curr);
		dns[1] = ip4_addr2(dns_curr);
		dns[2] = ip4_addr3(dns_curr);
		dns[3] = ip4_addr4(dns_curr);

		scr_printf(	"IP:\t%d.%d.%d.%d\n"
				"NM:\t%d.%d.%d.%d\n"
				"GW:\t%d.%d.%d.%d\n"
				"DNS:\t%d.%d.%d.%d\n",
					ip_address[0], ip_address[1], ip_address[2], ip_address[3],
					netmask[0], netmask[1], netmask[2], netmask[3],
					gateway[0], gateway[1], gateway[2], gateway[3],
					dns[0], dns[1], dns[2], dns[3]);
	}
	else
	{
		scr_printf("Unable to read IP address.\n");
	}
}

static void ethPrintLinkStatus(void)
{
	int mode, baseMode;

	//SMAP is registered as the "sm0" device to the TCP/IP stack.
	scr_printf("Link:\t");
	if (NetManIoctl(NETMAN_NETIF_IOCTL_GET_LINK_STATUS, NULL, 0, NULL, 0) == NETMAN_NETIF_ETH_LINK_STATE_UP)
		scr_printf("Up\n");
	else
		scr_printf("Down\n");

	scr_printf("Mode:\t");
	mode = NetManIoctl(NETMAN_NETIF_IOCTL_ETH_GET_LINK_MODE, NULL, 0, NULL, 0);

	//NETMAN_NETIF_ETH_LINK_MODE_PAUSE is a flag, so file it off first.
	baseMode = mode & (~NETMAN_NETIF_ETH_LINK_DISABLE_PAUSE);
	switch(baseMode)
	{
		case NETMAN_NETIF_ETH_LINK_MODE_10M_HDX:
			scr_printf("10M HDX");
			break;
		case NETMAN_NETIF_ETH_LINK_MODE_10M_FDX:
			scr_printf("10M FDX");
			break;
		case NETMAN_NETIF_ETH_LINK_MODE_100M_HDX:
			scr_printf("100M HDX");
			break;
		case NETMAN_NETIF_ETH_LINK_MODE_100M_FDX:
			scr_printf("100M FDX");
			break;
		default:
			scr_printf("Unknown");
	}
	if(!(mode & NETMAN_NETIF_ETH_LINK_DISABLE_PAUSE))
		scr_printf(" with ");
	else
		scr_printf(" without ");
	scr_printf("Flow Control\n");
}

static void start_hdld_server(void);
static void stop_hdld_server(void);

static void poweroffCallback(void *arg);

static const char *device_point = "hdd0:";

int main(int argc, char *argv[])
{
	struct ip4_addr IP, NM, GW, DNS;
	int EthernetLinkMode;

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
	SifExecModuleBuffer(NETMAN_irx, size_NETMAN_irx, 0, NULL, NULL);
	SifExecModuleBuffer(SMAP_irx, size_SMAP_irx, 0, NULL, NULL);

	SifExecModuleBuffer(IOMANX_irx, size_IOMANX_irx, 0, NULL, NULL);
	SifExecModuleBuffer(FILEXIO_irx, size_FILEXIO_irx, 0, NULL, NULL);

	SifExecModuleBuffer(POWEROFF_irx, size_POWEROFF_irx, 0, NULL, NULL);
	poweroffInit();
	poweroffSetCallback(&poweroffCallback, NULL);

	//Initialize NETMAN
	NetManInit();

	init_scr();

	//The network interface link mode/duplex can be set.
	EthernetLinkMode = NETMAN_NETIF_ETH_LINK_MODE_AUTO;

	//Attempt to apply the new link setting.
	if(ethApplyNetIFConfig(EthernetLinkMode) != 0) {
		scr_printf("Error: failed to set link mode.\n");
		goto end;
	}

	//Initialize IP address.
	//In this example, DHCP is enabled, hence the IP, NM, GW and DNS fields are cleared to 0..
	ip4_addr_set_zero(&IP);
	ip4_addr_set_zero(&NM);
	ip4_addr_set_zero(&GW);
	ip4_addr_set_zero(&DNS);

	//Initialize the TCP/IP protocol stack.
	ps2ipInit(&IP, &NM, &GW);

	//Enable DHCP
	ethApplyIPConfig(1, &IP, &NM, &GW, &DNS);

	//Wait for the link to become ready.
	scr_printf("Waiting for connection...\n");
	if(ethWaitValidNetIFLinkState() != 0) {
		scr_printf("Error: failed to get valid link status.\n");
		goto end;
	}

	scr_printf("Waiting for DHCP lease...");
	//Wait for DHCP to initialize, if DHCP is enabled.
	if (ethWaitValidDHCPState() != 0)
	{
		scr_printf("DHCP failed\n.");
		goto end;
	}
	scr_printf("done!\n");

	scr_printf("Initialized:\n");
	ethPrintLinkStatus();
	ethPrintIPConfig();

	scr_printf("Initing HDD:\n");
	//Load modules
	SifExecModuleBuffer(ATAD_irx, size_ATAD_irx, 0, NULL, NULL);
	SifExecModuleBuffer(HDD_irx, size_HDD_irx, 0, NULL, NULL);

	scr_printf("Initing DVR:\n");
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

	scr_printf("Initing HDLD server:\n");
    start_hdld_server();
	//At this point, network support has been initialized and the PS2 can be pinged.
	scr_printf("Everything inited!\n");
	SleepThread();

end:
	//To cleanup, just call these functions.
	ps2ipDeinit();
	NetManDeinit();

	//Deinitialize SIF services
	SifExitRpc();

	return 0;
}

u8 tcp_server_stack[0x1000] __attribute__((aligned(16)));
u8 udp_server_stack[0x1000] __attribute__((aligned(16)));

#define mips_memcpy memcpy
#define mips_memset memset

static inline int CreateMutex(int state)
{
    ee_sema_t sema;
    sema.option  = 0;
    sema.init_count = state;
    sema.max_count     = 1;
    return CreateSema(&sema);
}

typedef struct _ata_devinfo
{
    /** Total number of user sectors.  */
    u32 total_sectors;
} ata_devinfo_t;

static ata_devinfo_t ata_devinfo;

extern void *_gp;

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
	ata_devinfo.total_sectors = //fileXioDevctl(device_point, HDIOC_TOTALSECTOR, NULL, 0, NULL, 0);
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
    stop_hdld_server();

    scr_printf("dev9x off\n");
    while (fileXioDevctl("dev9x:", DDIOC_OFF, NULL, 0, NULL, 0) < 0);

    scr_printf("Shut down!!!\n");
    poweroffShutdown();
}

// hdldsvr from OPL starts

#define FAKE_WRITES

// #define TCP_SERVER_PORT 45233
// #define UDP_SERVER_PORT 45233
#define TCP_SERVER_PORT 12345
#define UDP_SERVER_PORT 12345

#define CMD_SIZE 16
#define CMD_STAT 0x73746174 // 'stat'; get HDD size in KB
#define CMD_READ 0x72656164 // 'read'; read sectors from HDD
#define CMD_WRIT 0x77726974 // 'writ'; write sectors to HDD
#define CMD_WRIS 0x77726973 // 'wris'; get last write status
#define CMD_FLSH 0x666c7368 // 'flsh'; flush write buff
#define CMD_POWX 0x706f7778 // 'powx'; poweroff system

#define HDD_SECTOR_SIZE 512  // HDD sector size in bytes
#define NET_NUM_SECTORS 2048 // max # of sectors to move via network

#define STATUS_AVAIL 0        // expecting command
#define STATUS_BUSY_RESPOND 1 // sending back response (and readen data)
#define STATUS_BUSY_WRITE 2   // expecting write data
#define STATUS_WRITE_STAT 3   // sending back write stat response

#define SETBIT(mask, bit) (mask)[(bit) / 32] |= 1 << ((bit) % 32)
#define GETBIT(mask, bit) ((mask)[(bit) / 32] & (1 << ((bit) % 32)))

void tcp_server_thread(void *args);
void udp_server_thread(void *args);

static int tcp_server_tid, udp_server_tid;
static int udp_mutex = -1;

static u8 tcp_buf[CMD_SIZE + HDD_SECTOR_SIZE * 2] __attribute__((aligned(64)));
static u8 udp_buf[8 + HDD_SECTOR_SIZE * 2] __attribute__((aligned(64)));

struct tcp_packet_header
{
    u32 command;
    u32 start_sector;
    u32 num_sectors;
    u32 result;
} __attribute__((packed));

typedef struct
{
    struct tcp_packet_header hdr;
    u8 data[HDD_SECTOR_SIZE * 2];
} tcp_packet_t __attribute__((packed));

typedef struct
{
    u8 data[HDD_SECTOR_SIZE * 2];
    u32 command;
    u32 start_sector;
} udp_packet_t __attribute__((packed));

struct stats
{
    int status;
    u32 command;
    u32 start_sector;
    u32 num_sectors;
    u8 *data;
    u32 bitmask[(NET_NUM_SECTORS + 31) / 32];
    u32 bitmask_copy[(NET_NUM_SECTORS + 31) / 32];      // endian-neutral
    u8 buffer[HDD_SECTOR_SIZE * (NET_NUM_SECTORS + 1)]; // 1MB on IOP !!! Huge !
};

static struct stats gstats __attribute__((aligned(64)));

//-------------------------------------------------------------------------
// modified for EE
static void start_hdld_server(void)
{
    ee_thread_t thread_param;

    // create a locked udp mutex
    udp_mutex = CreateMutex(0);

    // create & start the tcp thread
    thread_param.func             = (void *)tcp_server_thread;
    thread_param.stack            = tcp_server_stack;
    thread_param.stack_size       = sizeof(tcp_server_stack);
    thread_param.gp_reg           = &_gp;
    thread_param.option           = 0;
    thread_param.initial_priority = 0x10;
    tcp_server_tid = CreateThread(&thread_param);

    StartThread(tcp_server_tid, 0);

    // create & start the udp thread
    thread_param.func             = (void *)udp_server_thread;
    thread_param.stack            = udp_server_stack;
    thread_param.stack_size       = sizeof(udp_server_stack);
    thread_param.gp_reg           = &_gp;
    thread_param.option           = 0;
    thread_param.initial_priority = 0x10;
    udp_server_tid = CreateThread(&thread_param);

    StartThread(udp_server_tid, 0);
}

//-------------------------------------------------------------------------
// modified for EE
static void stop_hdld_server(void)
{
    // delete threads
    DeleteThread(tcp_server_tid);
    DeleteThread(udp_server_tid);

    // delete udp mutex
    DeleteSema(udp_mutex);
}

//-------------------------------------------------------------------------
static int ack_tcp_command(int sock, void *buf, int bsize)
{
    register int r;

    // acknowledge a received command

    gstats.status = STATUS_BUSY_RESPOND;

    r = lwip_send(sock, buf, bsize, 0);

    gstats.status = STATUS_AVAIL;

    return r;
}

//-------------------------------------------------------------------------
static void reject_tcp_command(int sock, char *error_string)
{
    // reject a received command

    gstats.status = STATUS_BUSY_RESPOND;

    scr_printf("Err:%s\n", error_string);
    lwip_send(sock, error_string, strlen(error_string), 0);

    gstats.status = STATUS_AVAIL;

    ata_device_flush_cache(0);
}

//-------------------------------------------------------------------------
static int check_datas(u32 command, u32 start_sector, u32 num_sectors)
{
    // check if all write data is here

    register int i, got_all_datas = 1;

    // wait that udp mutex is unlocked
    WaitSema(udp_mutex);

    for (i = 0; i < num_sectors; ++i) {
        if (!GETBIT(gstats.bitmask, i)) {
            got_all_datas = 0;
            break;
        }
    }

    if (!got_all_datas) // some parts are missing; should ask for retransmit
        return -1;

    // all data here -- we can commit
    gstats.command = command;
    return 0;
}

//-------------------------------------------------------------------------
static int handle_tcp_client(int tcp_client_socket)
{
    register int r, i, size;
    ata_devinfo_t *devinfo;
    tcp_packet_t *pkt = (tcp_packet_t *)tcp_buf;

    while (1) {

        // receive incoming packets
        size = lwip_recv(tcp_client_socket, &tcp_buf[0], sizeof(tcp_buf), 0);
        if (size < 0)
            goto error;

        // check if valid command size
        if (size != CMD_SIZE) {
            reject_tcp_command(tcp_client_socket, "handle_recv: invalid packet received");
            goto error;
        }

        // don't accept 'wris' without previous 'writ'
        if ((gstats.status == STATUS_AVAIL) && (pkt->hdr.command == CMD_WRIS)) {
            reject_tcp_command(tcp_client_socket, "write stat denied");
            goto error;
        } else if ((!(gstats.status == STATUS_BUSY_WRITE) && (pkt->hdr.command == CMD_WRIS))) {
            reject_tcp_command(tcp_client_socket, "busy");
            goto error;
        }

        switch (pkt->hdr.command) {

            // ------------------------------------------------
            // 'writ' command
            // ------------------------------------------------
            case CMD_WRIT:
                // confirm write
                pkt->hdr.result = 0;
                r = ack_tcp_command(tcp_client_socket, &tcp_buf[0], sizeof(struct tcp_packet_header));
                if (r < 0) {
                    reject_tcp_command(tcp_client_socket, "init_write b0rked");
                    goto error;
                }

                gstats.status = STATUS_BUSY_WRITE;
                gstats.command = pkt->hdr.command;
                gstats.start_sector = pkt->hdr.start_sector;
                gstats.num_sectors = pkt->hdr.num_sectors;

                // set-up buffer and clear bitmask
                gstats.data = (u8 *)(((long)&gstats.buffer[HDD_SECTOR_SIZE - 1]) & ~(HDD_SECTOR_SIZE - 1));
                mips_memset(gstats.bitmask, 0, sizeof(gstats.bitmask));

                break;


            // ------------------------------------------------
            // 'wris' command
            // ------------------------------------------------
            case CMD_WRIS:
                if ((pkt->hdr.start_sector != gstats.start_sector) || (pkt->hdr.num_sectors != gstats.num_sectors)) {
                    reject_tcp_command(tcp_client_socket, "invalid write stat");
                    goto error;
                }
                r = check_datas(pkt->hdr.command, pkt->hdr.start_sector, pkt->hdr.num_sectors);
                if (r < 0) {

                    // means we need retransmission of some datas so we send bitmask stats to the client
                    u32 *out = gstats.bitmask_copy;
                    for (i = 0; i < (NET_NUM_SECTORS + 31) / 32; ++i)
                        out[i] = gstats.bitmask[i];

                    mips_memcpy(pkt->data, (void *)out, sizeof(gstats.bitmask_copy));

                    pkt->hdr.result = 0;
                    r = ack_tcp_command(tcp_client_socket, &tcp_buf[0], sizeof(struct tcp_packet_header) + sizeof(gstats.bitmask));
                    if (r < 0) {
                        reject_tcp_command(tcp_client_socket, "init_write_stat failed");
                        goto error;
                    }

                    gstats.status = STATUS_BUSY_WRITE;
                } else {
                    pkt->hdr.result = pkt->hdr.num_sectors;

#ifdef FAKE_WRITES
                    // fake write, we read instead
                    ata_device_sector_io(0, gstats.data, pkt->hdr.start_sector, pkt->hdr.num_sectors, ATA_DIR_READ);
#else
                    // !!! real writes !!!
                    ata_device_sector_io(0, gstats.data, pkt->hdr.start_sector, pkt->hdr.num_sectors, ATA_DIR_WRITE);
#endif
                    ack_tcp_command(tcp_client_socket, &tcp_buf[0], sizeof(struct tcp_packet_header));
                }
                break;


            // ------------------------------------------------
            // 'stat' command
            // ------------------------------------------------
            case CMD_STAT:
                if ((devinfo = ata_get_devinfo(0)) == NULL)
                    pkt->hdr.result = -1;
                else
                    pkt->hdr.result = devinfo->total_sectors >> 1;
                ack_tcp_command(tcp_client_socket, &tcp_buf[0], sizeof(struct tcp_packet_header));

                break;


            // ------------------------------------------------
            // 'read' command
            // ------------------------------------------------
            case CMD_READ:
                // set up buffer
                gstats.data = (u8 *)(((long)&gstats.buffer + HDD_SECTOR_SIZE - 1) & ~(HDD_SECTOR_SIZE - 1));

                if (pkt->hdr.num_sectors > 2) {
                    r = ata_device_sector_io(0, gstats.data, pkt->hdr.start_sector, pkt->hdr.num_sectors, ATA_DIR_READ);
                    if (r != 0)
                        pkt->hdr.result = -1;
                    else
                        pkt->hdr.result = pkt->hdr.num_sectors;

                    ack_tcp_command(tcp_client_socket, &tcp_buf[0], sizeof(struct tcp_packet_header));
                    if (r == 0)
                        ack_tcp_command(tcp_client_socket, gstats.data, pkt->hdr.num_sectors * HDD_SECTOR_SIZE);
                } else {
                    r = ata_device_sector_io(0, pkt->data, pkt->hdr.start_sector, pkt->hdr.num_sectors, ATA_DIR_READ);
                    if (r != 0)
                        pkt->hdr.result = -1;
                    else
                        pkt->hdr.result = pkt->hdr.num_sectors;

                    ack_tcp_command(tcp_client_socket, &tcp_buf[0], sizeof(tcp_packet_t));
                }

                break;


            // ------------------------------------------------
            // 'flsh' command
            // ------------------------------------------------
            case CMD_FLSH:
                ata_device_flush_cache(0);

                break;


            // ------------------------------------------------
            // 'powx' command
            // ------------------------------------------------
            case CMD_POWX:
            	scr_printf("Powering off\n");
                ata_device_flush_cache(0);
                dev9Shutdown();
                PoweroffShutdown();

                break;



            default:
                reject_tcp_command(tcp_client_socket, "handle_recv: unknown command");
                goto error;
        }
    }

    return 0;

error:
    return -1;
}

//-------------------------------------------------------------------------
void tcp_server_thread(void *args)
{
    int tcp_socket, client_socket = -1;
    struct sockaddr_in peer;
    int peerlen;
    register int r;

    while (1) {

        peer.sin_family = AF_INET;
        peer.sin_port = htons(TCP_SERVER_PORT);
        peer.sin_addr.s_addr = htonl(INADDR_ANY);

        // create the socket
        tcp_socket = lwip_socket(AF_INET, SOCK_STREAM, 0);
        if (tcp_socket < 0)
            goto error;

        // bind the socket
        r = lwip_bind(tcp_socket, (struct sockaddr *)&peer, sizeof(peer));
        if (r < 0)
            goto error;

        // we want to listen
        r = lwip_listen(tcp_socket, 3);
        if (r < 0)
            goto error;

        while (1) {
            peerlen = sizeof(peer);
            // wait for incoming connection
            client_socket = lwip_accept(tcp_socket, (struct sockaddr *)&peer, &peerlen);
            if (client_socket < 0)
                goto error;

            // got connection, handle the client
            r = handle_tcp_client(client_socket);
            if (r < 0)
                lwip_close(client_socket);
        }

    error:
        // close the socket
        lwip_close(tcp_socket);
    }
}

//-------------------------------------------------------------------------
void udp_server_thread(void *args)
{
    int udp_socket;
    struct sockaddr_in peer;
    register int r;
    udp_packet_t *pkt = (udp_packet_t *)udp_buf;

    while (1) {

        peer.sin_family = AF_INET;
        peer.sin_port = htons(UDP_SERVER_PORT);
        peer.sin_addr.s_addr = htonl(INADDR_ANY);

        // create the socket
        udp_socket = lwip_socket(AF_INET, SOCK_DGRAM, 0);
        if (udp_socket < 0)
            goto error;

        // bind the socket
        r = lwip_bind(udp_socket, (struct sockaddr *)&peer, sizeof(peer));
        if (r < 0)
            goto error;

        while (1) {

            // wait for packet
            r = lwip_recv(udp_socket, udp_buf, sizeof(udp_buf), 0);

            // check to see if it's the command we're expecting
            if ((r == sizeof(udp_packet_t)) && (pkt->command == gstats.command)) {

                // check the start sector is valid
                if ((gstats.start_sector <= pkt->start_sector) && (pkt->start_sector < gstats.start_sector + gstats.num_sectors)) {

                    u32 start_sector = pkt->start_sector - gstats.start_sector;

                    if (!GETBIT(gstats.bitmask, start_sector)) {
                        mips_memcpy(gstats.data + start_sector * HDD_SECTOR_SIZE, pkt, HDD_SECTOR_SIZE * 2);

                        SETBIT(gstats.bitmask, start_sector);
                        SETBIT(gstats.bitmask, start_sector + 1);

                        // unlock udp mutex
                        SignalSema(udp_mutex);
                    }
                }
            }
        }

    error:
        // close the socket
        lwip_close(udp_socket);
    }
}