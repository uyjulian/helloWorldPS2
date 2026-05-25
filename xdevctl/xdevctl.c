
#include <irx_imports.h>
#include <loadcore.h>
#include <errno.h>

struct rpc_78444556_stru
{
	int m_cmd;
	int m_arglen;
	int m_buflen;
	char m_name[0x34];
	char m_arg[0x400];
	int m_retval;
	char m_buf[0x43C];
};

static struct rpc_78444556_stru g_rpc_buf_78444556 __attribute__((__aligned__(4)));

static void *rpc_handler_78444556(int fno, void *buffer, int length)
{
	g_rpc_buf_78444556.m_retval = iomanX_devctl(g_rpc_buf_78444556.m_name, g_rpc_buf_78444556.m_cmd, g_rpc_buf_78444556.m_arg, g_rpc_buf_78444556.m_arglen, g_rpc_buf_78444556.m_buf, g_rpc_buf_78444556.m_buflen);
	return &g_rpc_buf_78444556.m_retval;
}

struct rpc_79444556_stru
{
	int m_cmd;
	int m_arglen;
	int m_buflen;
	char *m_name;
	void *m_arg;
	void *m_buf;
};

static struct rpc_79444556_stru g_rpc_buf_79444556 __attribute__((__aligned__(4)));

static void *rpc_handler_79444556(int fno, void *buffer, int length)
{
	static int ret;
	ret = iomanX_devctl(g_rpc_buf_79444556.m_name, g_rpc_buf_79444556.m_cmd, g_rpc_buf_79444556.m_arg, g_rpc_buf_79444556.m_arglen, g_rpc_buf_79444556.m_buf, g_rpc_buf_79444556.m_buflen);
	return &ret;
}

static SifRpcDataQueue_t g_qd;
static SifRpcServerData_t g_sd1;
static SifRpcServerData_t g_sd2;

static void thproc_sifrpc_78444556(void *)
{
	sceSifInitRpc(0);
	sceSifSetRpcQueue(&g_qd, GetThreadId());
	sceSifRegisterRpc(&g_sd1, 0x78444556, rpc_handler_78444556, &g_rpc_buf_78444556, 0, 0, &g_qd);
	sceSifRegisterRpc(&g_sd2, 0x79444556, rpc_handler_79444556, &g_rpc_buf_79444556, 0, 0, &g_qd);
	sceSifRpcLoop(&g_qd);
}

static int g_thid;

int _start(int argc, char* argv[])
{
	iop_thread_t thparam;

	thparam.attr = TH_C;
	thparam.thread = thproc_sifrpc_78444556;
	thparam.priority = 32;
	thparam.stacksize = 0x4000;
	thparam.option = 0;
	g_thid = CreateThread(&thparam);
	StartThread(g_thid, 0);
	return MODULE_RESIDENT_END;
	// return ( g_thid < 0 || StartThread(g_thid, 0) < 0 ) ? MODULE_NO_RESIDENT_END : MODULE_RESIDENT_END;
}
