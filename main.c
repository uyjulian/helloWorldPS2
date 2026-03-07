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
#include <arpa/inet.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <stdlib.h>
#include <string.h>
#include <hdd-ioctl.h>
#include <libpwroff.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>

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

static void start_iperf_server(void);
static void stop_iperf_server(void);

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

	scr_printf("Initing iperf server:\n");
    start_iperf_server();
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

static void poweroffCallback(void *arg)
{
	scr_printf("Powering off\n");
    stop_iperf_server();

    scr_printf("dev9x off\n");
    while (fileXioDevctl("dev9x:", DDIOC_OFF, NULL, 0, NULL, 0) < 0);

    scr_printf("Shut down!!!\n");
    poweroffShutdown();
}

#if 0
void tcp_server_thread(void *args);
#endif
void udp_server_thread(void *args);

#if 0
static int tcp_server_tid;
#endif
static int udp_server_tid;

#if 0
static u8 tcp_buf[1048576 * 8] __attribute__((aligned(64)));
#endif
static u8 udp_buf[1048576 * 8] __attribute__((aligned(64)));

//-------------------------------------------------------------------------
// modified for EE
static void start_iperf_server(void)
{
    ee_thread_t thread_param;

#if 0
    // create & start the tcp thread
    thread_param.func             = (void *)tcp_server_thread;
    thread_param.stack            = tcp_server_stack;
    thread_param.stack_size       = sizeof(tcp_server_stack);
    thread_param.gp_reg           = &_gp;
    thread_param.option           = 0;
    thread_param.initial_priority = 0x10;
    tcp_server_tid = CreateThread(&thread_param);

    StartThread(tcp_server_tid, 0);
#endif

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
static void stop_iperf_server(void)
{
    // delete threads
#if 0
    DeleteThread(tcp_server_tid);
#endif
    DeleteThread(udp_server_tid);
}
// The following is based on
// https://github.com/Xilinx/embeddedsw/blob/1bb19ac1ab06ab322ba4340bed372f93ca612a18/lib/sw_apps/lwip_udp_perf_server/src/udp_perf_server.c
// https://github.com/Xilinx/embeddedsw/blob/1bb19ac1ab06ab322ba4340bed372f93ca612a18/lib/sw_apps/lwip_udp_perf_server/src/udp_perf_server.h
/*
 * Copyright (C) 2018 - 2022 Xilinx, Inc. All rights reserved.
 * Copyright (C) 2022 - 2024 Advanced Micro Devices, Inc.  All rights reserved
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 */

/** Connection handle for a UDP Server session */

#include <stdint.h>
#include <time.h>

#define s8_t int8_t
#define s16_t int16_t
#define s32_t int32_t
#define s64_t int64_t
#define u8_t uint8_t
#define u16_t uint16_t
#define u32_t uint32_t
#define u64_t uint64_t

#define xil_printf scr_printf

static u64_t get_time_ms(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);;
	return (((u64)tv.tv_usec / 1000) + ((u64)tv.tv_sec * 1000)) / 50; // TODO: why 50?
}

/* used as indices into kLabel[] */
enum {
	KCONV_UNIT,
	KCONV_KILO,
	KCONV_MEGA,
	KCONV_GIGA,
};

/* labels for formats [KMG] */
const char kLabel[] =
{
	' ',
	'K',
	'M',
	'G'
};

/* used as type of print */
enum measure_t {
	BYTES,
	SPEED
};

/* Report type */
enum report_type {
	/* The Intermediate report */
	INTER_REPORT,
	/* The server side test is done */
	UDP_DONE_SERVER,
	/* Remote side aborted the test */
	UDP_ABORTED_REMOTE
};

struct interim_report {
	u64_t start_time;
	u64_t last_report_time;
	u32_t total_bytes;
	u32_t cnt_datagrams;
	u32_t cnt_dropped_datagrams;
};

struct perf_stats {
	u8_t client_id;
	u64_t start_time;
	u64_t end_time;
	u64_t total_bytes;
	u64_t cnt_datagrams;
	u64_t cnt_dropped_datagrams;
	u32_t cnt_out_of_order_datagrams;
	s32_t expected_datagram_id;
	struct interim_report i_report;
};

/* seconds between periodic bandwidth reports */
#define INTERIM_REPORT_INTERVAL 5

/* server port to listen on/connect to */
#define UDP_CONN_PORT 5001

static t_ip_info ip_info;
static struct sockaddr_in sa;
static int sa_len;
static struct perf_stats server;
/* Report interval in ms */
#define REPORT_INTERVAL_TIME (INTERIM_REPORT_INTERVAL * 20)

void print_app_header(void)
{
	xil_printf("UDP server listening on port %d\r\n",
			UDP_CONN_PORT);
	xil_printf("On Host: Run $iperf -c %s -i %d -t 300 -u -b <bandwidth>\r\n",
			inet_ntoa(ip_info.ipaddr),
			INTERIM_REPORT_INTERVAL);

}

static void print_udp_conn_stats(void)
{
	xil_printf("[%3d] local %s port %d connected with ",
			server.client_id, inet_ntoa(ip_info.ipaddr),
			UDP_CONN_PORT);
	xil_printf("%s port %d\r\n", inet_ntoa(sa.sin_addr),
			ntohs(sa.sin_port));
	xil_printf("[ ID] Interval\t     Transfer     Bandwidth\t");
	xil_printf("    Lost/Total Datagrams\n\r");
}

static void stats_buffer(char* outString,
		double data, enum measure_t type)
{
	int conv = KCONV_UNIT;
	const char *format;
	double unit = 1024.0;

	if (type == SPEED)
		unit = 1000.0;

	while (data >= unit && conv <= KCONV_GIGA) {
		data /= unit;
		conv++;
	}

	/* Fit data in 4 places */
	if (data < 9.995) { /* 9.995 rounded to 10.0 */
		format = "%4.2f %c"; /* #.## */
	} else if (data < 99.95) { /* 99.95 rounded to 100 */
		format = "%4.1f %c"; /* ##.# */
	} else {
		format = "%4.0f %c"; /* #### */
	}
	sprintf(outString, format, data, kLabel[conv]);
}


/** The report function of a TCP server session */
static void udp_conn_report(u64_t diff,
		enum report_type report_type)
{
	u64_t total_len, cnt_datagrams, cnt_dropped_datagrams, total_packets;
	u32_t cnt_out_of_order_datagrams;
	double duration, bandwidth = 0;
	char data[16], perf[16], time[64], drop[64];

	if (report_type == INTER_REPORT) {
		total_len = server.i_report.total_bytes;
		cnt_datagrams = server.i_report.cnt_datagrams;
		cnt_dropped_datagrams = server.i_report.cnt_dropped_datagrams;
	} else {
		server.i_report.last_report_time = 0;
		total_len = server.total_bytes;
		cnt_datagrams = server.cnt_datagrams;
		cnt_dropped_datagrams = server.cnt_dropped_datagrams;
		cnt_out_of_order_datagrams = server.cnt_out_of_order_datagrams;
	}

	total_packets = cnt_datagrams + cnt_dropped_datagrams;
	/* Converting duration from milliseconds to secs,
	 * and bandwidth to bits/sec .
	 */
	duration = diff / 20.0; /* secs */
	if (duration)
		bandwidth = (total_len / duration) * 8.0;

	stats_buffer(data, total_len, BYTES);
	stats_buffer(perf, bandwidth, SPEED);
	/* On 32-bit platforms, xil_printf is not able to print
	 * u64_t values, so converting these values in strings and
	 * displaying results
	 */
	sprintf(time, "%4.1f-%4.1f sec",
			(double)server.i_report.last_report_time,
			(double)(server.i_report.last_report_time + duration));
	sprintf(drop, "%4llu/%5llu (%.2g%%)", cnt_dropped_datagrams,
			total_packets,
			(100.0 * cnt_dropped_datagrams)/total_packets);
	xil_printf("[%3d] %s  %sBytes  %sbits/sec  %s\n\r", server.client_id,
			time, data, perf, drop);

	if (report_type == INTER_REPORT) {
		server.i_report.last_report_time += duration;
	} else if ((report_type != INTER_REPORT) && cnt_out_of_order_datagrams) {
		xil_printf("[%3d] %s  %u datagrams received out-of-order\n\r",
				server.client_id, time,
				cnt_out_of_order_datagrams);
	}
}


static void reset_stats(void)
{
	server.client_id++;
	/* Save start time */
	server.start_time = get_time_ms();
	server.end_time = 0; /* ms */
	server.total_bytes = 0;
	server.cnt_datagrams = 0;
	server.cnt_dropped_datagrams = 0;
	server.cnt_out_of_order_datagrams = 0;
	server.expected_datagram_id = 0;

	/* Initialize Interim report parameters */
	server.i_report.start_time = 0;
	server.i_report.total_bytes = 0;
	server.i_report.cnt_datagrams = 0;
	server.i_report.cnt_dropped_datagrams = 0;
	server.i_report.last_report_time = 0;
}


void udp_server_thread(void *args)
{
    int udp_socket;
    struct sockaddr_in peer;
    register int r;
	static u8_t first = 1;
	static u64_t now;

    ps2ip_getconfig("sm0", &ip_info);

    print_app_header();

    while (1) {

        peer.sin_family = AF_INET;
        peer.sin_port = htons(UDP_CONN_PORT);
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
            sa_len = sizeof(sa);
            r = lwip_recvfrom(udp_socket, udp_buf, sizeof(udp_buf), 0, (struct sockaddr *)&sa, &sa_len);
            if (r >= 0)
            {

				u32_t drop_datagrams = 0;
				s32_t recv_id;

				/* first, check if the datagram is received in order */
#ifdef __MICROBLAZE__
				/* For Microblaze, word access are at 32 bit boundaries.
				 * To read complete 4 byte of UDP ID from data payload,
				 * we should read upper 2 bytes from current word boundary
				 * of payload and lower 2 bytes from next word boundary of
				 * payload.
				 */
				s16_t *payload;
				payload = (s16_t *) (udp_buf);
				recv_id = (ntohs(payload[0]) << 16) | ntohs(payload[1]);
#else
				recv_id = ntohl(*((int *)(udp_buf)));
#endif
				if (first && (recv_id == 0 || recv_id == 1)) {
					/* First packet should always start with recv id 0.
					 * However, If Iperf client is running with parallel
					 * thread, then this condition will also avoid
					 * multiple print of connection header
					 */
					reset_stats();
					/* Print connection statistics */
					print_udp_conn_stats();
					first = 0;
				} else if (first) {
					/* Avoid rest of the packets if client
					 * connection is already terminated.
					 */
					return;
				}

				if (recv_id < 0) {
					u64_t diff_ms = now - server.start_time;
					/* Send Ack */
					lwip_sendto(udp_socket, udp_buf, r, 0, (struct sockaddr *)&sa, sa_len);
					udp_conn_report(diff_ms, UDP_DONE_SERVER);
					xil_printf("UDP test passed Successfully\n\r");
					first = 1;
					return;
				}

				/* Update dropped datagrams statistics */
				if (server.expected_datagram_id != recv_id) {
					if (server.expected_datagram_id < recv_id) {
						drop_datagrams =
							recv_id - server.expected_datagram_id;
						server.cnt_dropped_datagrams += drop_datagrams;
						server.expected_datagram_id = recv_id + 1;
					} else if (server.expected_datagram_id > recv_id) {
						server.cnt_out_of_order_datagrams++;
					}
				} else {
					server.expected_datagram_id++;
				}

				server.cnt_datagrams++;

				/* Record total bytes for final report */
				server.total_bytes += r;

				if (REPORT_INTERVAL_TIME) {
					now = get_time_ms();

					server.i_report.cnt_datagrams++;
					server.i_report.cnt_dropped_datagrams += drop_datagrams;

					/* Record total bytes for interim report */
					server.i_report.total_bytes += r;
					if (server.i_report.start_time) {
						u64_t diff_ms = now - server.i_report.start_time;

						if (diff_ms >= REPORT_INTERVAL_TIME) {
							udp_conn_report(diff_ms, INTER_REPORT);
							/* Reset Interim report counters */
							server.i_report.start_time = 0;
							server.i_report.total_bytes = 0;
							server.i_report.cnt_datagrams = 0;
							server.i_report.cnt_dropped_datagrams = 0;
						}
					} else {
						/* Save start time for interim report */
						server.i_report.start_time = now;
					}
				}

            }

        }

    error:
        // close the socket
        lwip_close(udp_socket);
    }
}