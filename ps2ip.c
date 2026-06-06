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
#include <mongoose/mongoose.c>
#include <delaythread.h>
#include <ps2sdkapi.h>

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

static void start_mongoose_server(void);
static void stop_mongoose_server(void);

static void poweroffCallback(void *arg);

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

	fileXioInit();
    // Increase the FILEIO R/W buffer size to reduce overhead.
    fileXioSetRWBufferSize(0x40000);

	SifExecModuleBuffer(POWEROFF_irx, size_POWEROFF_irx, 0, NULL, NULL);
	poweroffInit();
	poweroffSetCallback(&poweroffCallback, NULL);

	//Initialize NETMAN
	NetManInit();

	init_scr();

	scr_printf("Need help? Please visit https://uyjulian.github.io/desr-help/\n");
	//The network interface link mode/duplex can be set.
	EthernetLinkMode = NETMAN_NETIF_ETH_LINK_MODE_AUTO;

	//Attempt to apply the new link setting.
	if(ethApplyNetIFConfig(EthernetLinkMode) != 0)
	{
		scr_printf("Error: failed to set link mode.\n");
		SleepThread();
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
	if (ethWaitValidNetIFLinkState() != 0)
	{
		scr_printf("Error: failed to get valid link status.\n");
		SleepThread();
		goto end;
	}

	scr_printf("Waiting for DHCP lease...");
	//Wait for DHCP to initialize, if DHCP is enabled.
	if (ethWaitValidDHCPState() != 0)
	{
		scr_printf("DHCP failed\n.");
		SleepThread();
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
    if (!fileXioDevctl("dvr_hdd0:", HDIOC_STATUS, NULL, 0, NULL, 0))
    {
    	scr_printf("DVR is available!\n");
    	{
    		// Set chunk buffer size
    		u32 xarg;
    		xarg = 0x20000;
    		fileXioDevctl("dvr_hdd0:", 0x5065, &xarg, sizeof(xarg), NULL, 0);
    	}
    }

	scr_printf("Initing Mongoose HTTP server:\n");
    start_mongoose_server();
	//At this point, network support has been initialized and the PS2 can be pinged.
	scr_printf("Everything inited!\n");
	t_ip_info ip_info;
	if (ps2ip_getconfig("sm0", &ip_info) >= 0)
	{
		scr_printf("On Host: Access http://%s:80/fs/dvr_hdd0/__xcontents/\n", inet_ntoa(ip_info.ipaddr));
		scr_printf("On Host: Access http://%s:80/fs/dvr_hdd0/__xdata/\n", inet_ntoa(ip_info.ipaddr));
	}
	SleepThread();

end:
	//To cleanup, just call these functions.
	ps2ipDeinit();
	NetManDeinit();

	//Deinitialize SIF services
	SifExitRpc();

	return 0;
}

extern void *_gp;

static void poweroffCallback(void *arg)
{
	scr_printf("Powering off\n");
    stop_mongoose_server();

    scr_printf("dev9x off\n");
    while (fileXioDevctl("dev9x:", DDIOC_OFF, NULL, 0, NULL, 0) < 0);

    scr_printf("Shut down!!!\n");
    poweroffShutdown();
}

static void mongoose_server_thread(void *args);
static void fillbuf_thread(void *args);

struct fillbuf_data
{
	int m_ret;
	int m_offset;
	int m_fd;
	int m_rdsema;
	int m_sdsema;
	u8 m_buf[MG_IO_SIZE] __attribute__((aligned(64)));
};
static int g_fillbuf_tid;
static int g_fillbuf_fd;
static int g_mongoose_server_tid;
static u8 g_fillbuf_stack[0x10000] __attribute__((aligned(16)));
static u8 g_mongoose_server_stack[0x10000] __attribute__((aligned(16)));
static struct fillbuf_data g_fillbuf_data[2];
static int g_fillbuf_curdata_read;
static int g_fillbuf_curdata_send;

//-------------------------------------------------------------------------
// modified for EE
static void start_mongoose_server(void)
{
    ee_thread_t thread_param;
    ee_sema_t sema_param;
    unsigned int i;

    memset(&sema_param, 0, sizeof(sema_param));
	sema_param.init_count = 0;
	sema_param.max_count = 1;
	sema_param.option = 0;
	for (i = 0; i < (sizeof(g_fillbuf_data)/sizeof(g_fillbuf_data[0])); i += 1)
	{
		g_fillbuf_data[i].m_rdsema = CreateSema(&sema_param);
		g_fillbuf_data[i].m_sdsema = CreateSema(&sema_param);
		g_fillbuf_data[i].m_fd = -1;
	}		
	g_fillbuf_curdata_read = 0;
	g_fillbuf_curdata_send = 0;
	g_fillbuf_fd = -1;

    thread_param.func             = (void *)fillbuf_thread;
    thread_param.stack            = g_fillbuf_stack;
    thread_param.stack_size       = sizeof(g_fillbuf_stack);
    thread_param.gp_reg           = &_gp;
    thread_param.option           = 0;
    thread_param.initial_priority = 0x10;
    g_fillbuf_tid = CreateThread(&thread_param);

    thread_param.func             = (void *)mongoose_server_thread;
    thread_param.stack            = g_mongoose_server_stack;
    thread_param.stack_size       = sizeof(g_mongoose_server_stack);
    thread_param.gp_reg           = &_gp;
    thread_param.option           = 0;
    thread_param.initial_priority = 0x10;
    g_mongoose_server_tid = CreateThread(&thread_param);

    StartThread(g_fillbuf_tid, 0);
    StartThread(g_mongoose_server_tid, 0);
}

//-------------------------------------------------------------------------
// modified for EE
static void stop_mongoose_server(void)
{
	int i;
    // delete threads
	for (i = 0; i < (sizeof(g_fillbuf_data)/sizeof(g_fillbuf_data[0])); i += 1)
	{
		DeleteSema(g_fillbuf_data[i].m_rdsema);
		DeleteSema(g_fillbuf_data[i].m_sdsema);
	}
    DeleteThread(g_fillbuf_tid);
    DeleteThread(g_mongoose_server_tid);
}

static void fillbuf_thread(void *args)
{
	while (1)
	{
		int curdata;
		curdata = g_fillbuf_curdata_read;
		WaitSema(g_fillbuf_data[curdata].m_rdsema);
		g_fillbuf_data[curdata].m_ret = read(g_fillbuf_data[curdata].m_fd, g_fillbuf_data[curdata].m_buf, sizeof(g_fillbuf_data[curdata].m_buf));
		g_fillbuf_data[curdata].m_offset = 0;
		SignalSema(g_fillbuf_data[curdata].m_sdsema);
		g_fillbuf_curdata_read ^= 1;
	}
}

#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

static int xp_stat(const char *path, size_t *size, time_t *mtime)
{
	struct stat st;
	if (stat(path, &st) != 0) return 0;
	if (size) *size = (size_t) st.st_size;
	if (mtime) *mtime = st.st_mtime;
	return MG_FS_READ | MG_FS_WRITE | (S_ISDIR(st.st_mode) ? MG_FS_DIR : 0);
}

static void xp_list(const char *dir, void (*fn)(const char *, void *),
									 void *userdata) {
	struct dirent *dp;
	DIR *dirp;
	if ((dirp = (opendir(dir))) == NULL) return;
	while ((dp = readdir(dirp)) != NULL) {
		if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..")) continue;
		fn(dp->d_name, userdata);
	}
	closedir(dirp);
}

static void *xp_open(const char *path, int flags) {
	int fd;

	if (flags != MG_FS_READ)
		return NULL;
	fd = open(path, (flags == MG_FS_READ ? O_RDONLY : (O_RDWR | O_CREAT | O_APPEND)) | O_BINARY | O_CLOEXEC, 0777);
	if (fd >= 0 && g_fillbuf_fd < 0) {
		g_fillbuf_fd = fd;
		g_fillbuf_curdata_send = g_fillbuf_curdata_read;
	}
	return (fd >= 0) ? (void *)(uiptr)fd : NULL;
}

static void xp_close(void *fp) {
	if ((int)(uiptr)fp == g_fillbuf_fd)
	{
		int i;
		WaitSema(g_fillbuf_data[g_fillbuf_curdata_send ^ 1].m_sdsema);
		for (i = 0; i < (sizeof(g_fillbuf_data)/sizeof(g_fillbuf_data[0])); i += 1)
		{
			g_fillbuf_data[i].m_fd = -1;
		}
		for (i = 0; i < (sizeof(g_fillbuf_data)/sizeof(g_fillbuf_data[0])); i += 1)
		{
			g_fillbuf_data[i].m_ret = 0;
			g_fillbuf_data[i].m_offset = 0;
		}
		g_fillbuf_fd = -1;
	}
	close((int)(uiptr)fp);
}

static size_t xp_read(void *fp, void *buf, size_t len) {
	size_t xlen;
	int fd;

	fd = (int)(uiptr)fp;
	if (fd != g_fillbuf_fd)
		return read(fd, buf, len);
	if (g_fillbuf_data[g_fillbuf_curdata_send].m_offset == g_fillbuf_data[g_fillbuf_curdata_send].m_ret || g_fillbuf_data[g_fillbuf_curdata_send].m_ret < 0)
	{
		if (g_fillbuf_data[g_fillbuf_curdata_send].m_ret <= 0 && g_fillbuf_data[g_fillbuf_curdata_send ^ 1].m_ret <= 0)
		{
			// Initial fill
			g_fillbuf_data[g_fillbuf_curdata_send].m_fd = fd;
			g_fillbuf_data[g_fillbuf_curdata_send ^ 1].m_fd = fd;
			SignalSema(g_fillbuf_data[g_fillbuf_curdata_send].m_rdsema);
			SignalSema(g_fillbuf_data[g_fillbuf_curdata_send ^ 1].m_rdsema);
			WaitSema(g_fillbuf_data[g_fillbuf_curdata_send].m_sdsema);
		}
		else
		{
			g_fillbuf_curdata_send ^= 1;
			WaitSema(g_fillbuf_data[g_fillbuf_curdata_send].m_sdsema);
			SignalSema(g_fillbuf_data[g_fillbuf_curdata_send ^ 1].m_rdsema);
		}
	}
	xlen = len;
	if (xlen > g_fillbuf_data[g_fillbuf_curdata_send].m_ret - g_fillbuf_data[g_fillbuf_curdata_send].m_offset)
	{
		xlen = g_fillbuf_data[g_fillbuf_curdata_send].m_ret - g_fillbuf_data[g_fillbuf_curdata_send].m_offset;
	}
	memcpy(buf, ((u8 *)g_fillbuf_data[g_fillbuf_curdata_send].m_buf) + g_fillbuf_data[g_fillbuf_curdata_send].m_offset, xlen);
	g_fillbuf_data[g_fillbuf_curdata_send].m_offset += xlen;
	return xlen;
}

static size_t xp_write(void *fp, const void *buf, size_t len) {
	// return write((int)(uiptr)fp, buf, len);
	return 0;
}

static size_t xp_seek(void *fp, size_t offset) {
	return (size_t)lseek64((int)(uiptr)fp, (off64_t)offset, SEEK_SET);
}

static bool xp_rename(const char *from, const char *to) {
	return rename(from, to) == 0;
}

static bool xp_remove(const char *path) {
	return remove(path) == 0;
}

static bool xp_mkdir(const char *path) {
	return mkdir(path, 0775) == 0;
}

static struct mg_fs g_mg_fs_ps2sdk_posix = {
	xp_stat,
	xp_list,
	xp_open,
	xp_close,
	xp_read,
	xp_write,
	xp_seek,
	xp_rename,
	xp_remove,
	xp_mkdir,
};

static int req_count = 0;

static void mongoose_ev_handler(struct mg_connection *c, int ev, void *ev_data)
{
	if (ev == MG_EV_HTTP_MSG) // HTTP Request
	{
		struct mg_http_message *hm = (struct mg_http_message *)ev_data;
		// Print the request to the screen with a request count
		scr_printf("%.*s %.*s (#%d)\n", (int)hm->method.len, hm->method.buf, (int)hm->uri.len, hm->uri.buf,
				   ++req_count);

#define EXAMPLE_USE_HDD
#ifdef EXAMPLE_USE_HDD
		// Set our root directory to the mounted hdd0:WWW partition
		struct mg_http_serve_opts opts = {
			.root_dir = "pfs0:.,/fs/dvr_hdd0/__xcontents/=dvr_pfs0:.,/fs/dvr_hdd0/__xdata/=dvr_pfs1:.",
			.mime_types = "*=application/octet-stream",
			.fs = &g_mg_fs_ps2sdk_posix,
		};
		// Allow Mongoose to serve the request
		mg_http_serve_dir(c, hm, &opts);
#else
		const char *response = "<h1>Hello! I'm mongoose running on the PS2!<h1/><br/>"
							   "Unfortunately, I can't serve you any files because you I'm not configured to serve a hard drive partition<br/>\n"
							   "I can tell you that your request was for %.*s with the method %.*s\n";

		mg_http_reply(c, 200, "", response, (int)hm->uri.len, hm->uri.buf, (int)hm->method.len, hm->method.buf);
#endif
	}
}

static void mongoose_server_thread(void *args)
{
	struct mg_mgr mgr;

	mg_log_set(MG_LL_NONE);
	// Init Mongoose
	mg_mgr_init(&mgr);

	// Listen on port 80. Set callback function
	if (!mg_http_listen(&mgr, "http://0.0.0.0:80", mongoose_ev_handler, &mgr)) {
		scr_printf("mg_http_listen failed\n");
		SleepThread();
	}

	// Loop forever, accepting new connections
	while (1) {
		mg_mgr_poll(&mgr, 0);
		DelayThread(5 * 1000);
	}

	// Clean up
	mg_mgr_free(&mgr);
}