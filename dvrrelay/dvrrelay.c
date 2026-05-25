
#include <irx_imports.h>
#include <loadcore.h>
#include <errno.h>

struct rpc_7A444556_header_stru
{
	int m_argoffset_be;
	int m_cmd_be;
	int m_buflen_be;
	int m_arglen_be;
};

struct rpc_7A444556_stru
{
	int m_input_buffer_length;
	int m_command;
	void *m_input_buffer;
	void *m_output_buffer;
	int m_timeout;
	int m_retval;
};

static struct rpc_7A444556_stru g_rpc_buf_7A444556;

static void *rpc_handler_7A444556(int fno, void *buffer, int length)
{
	drvdrv_exec_cmd_ack cmdack;

	cmdack.input_buffer_length = g_rpc_buf_7A444556.m_input_buffer_length;
	cmdack.command = g_rpc_buf_7A444556.m_command;
	cmdack.input_word_count = 0;
	cmdack.input_buffer = g_rpc_buf_7A444556.m_input_buffer;
	cmdack.output_buffer = g_rpc_buf_7A444556.m_output_buffer;
	cmdack.timeout = g_rpc_buf_7A444556.m_timeout;
	if (DvrdrvExecCmdAckDma2Comp(&cmdack))
		g_rpc_buf_7A444556.m_retval = -EIO;
	else if (cmdack.comp_status)
		g_rpc_buf_7A444556.m_retval = -EIO;
	else
		g_rpc_buf_7A444556.m_retval = (cmdack.return_result_word[0] << 16) + cmdack.return_result_word[1];
	return &g_rpc_buf_7A444556.m_retval;
}

static SifRpcDataQueue_t g_qd;
static SifRpcServerData_t g_sd;

static void thproc_sifrpc_7A444556(void *)
{
	sceSifInitRpc(0);
	sceSifSetRpcQueue(&g_qd, GetThreadId());
	sceSifRegisterRpc(&g_sd, 0x7A444556, rpc_handler_7A444556, &g_rpc_buf_7A444556, 0, 0, &g_qd);
	sceSifRpcLoop(&g_qd);
}

static int g_thid;

int _start(int argc, char* argv[])
{
#if 0
	iop_thread_t thparam;

	thparam.attr = 0x2000000;
	thparam.thread = thproc_sifrpc_7A444556;
	thparam.priority = 32;
	thparam.stacksize = 0x4000;
	thparam.option = 0;
	g_thid = CreateThread(&thparam);
	return ( g_thid < 0 || StartThread(g_thid, 0) < 0 ) ? MODULE_NO_RESIDENT_END : MODULE_RESIDENT_END;
#endif
	return MODULE_NO_RESIDENT_END;
}
