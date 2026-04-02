#include "transport_ir.h"
#include "ir.h"

static bool ir_backend_init(void)
{
	return ir_init();
}

static void ir_backend_enable(void)
{
	ir_enable();
}

static void ir_backend_disable(void)
{
	ir_disable();
}

static void ir_backend_send(void *data, u32 size)
{
	ir_send_data(data, size);
}

static u32 ir_backend_recv(void *data, u32 size, u32 timeout_ms)
{
	(void) timeout_ms;
	return ir_recv_data(data, size);
}

static const transport_backend g_ir_backend = {
	.name = "ir",
	.init = ir_backend_init,
	.enable = ir_backend_enable,
	.disable = ir_backend_disable,
	.send_data = ir_backend_send,
	.recv_data = ir_backend_recv,
	.cleanup = NULL,
};

const transport_backend *transport_ir_backend_get(void)
{
	return &g_ir_backend;
}
