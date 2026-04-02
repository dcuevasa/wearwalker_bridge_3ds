#include "transport.h"
#include "transport_ir.h"
#include "transport_wifi.h"
#include "ui.h"
#include "updates.h"
#include <stdio.h>
#include <stdlib.h>
#include <3ds.h>

int main(int argc, char* argv[])
{
	enum operation op;
	s32 prio;

	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);

	gfxInitDefault();

	ui_init();

#ifdef USE_WIFI_BACKEND
	transport_set_backend(transport_wifi_backend_get());
#else
	transport_set_backend(transport_ir_backend_get());
#endif

	if (!transport_init()) {
		printf("Error while initializing transport backend\n");
		ui_exit();
		gfxExit();
		return 1;
	}

	printf("Using %s backend\n", transport_get_backend()->name);

	ui_draw();
	// Disable updates checking and downloading for now, as it seems that httpc
	// doesn't work anymore on GitHub...
	// threadCreate((ThreadFunc) updates_check, (void *) VER, 1024, prio - 1, -2, true);
	while (aptMainLoop()) {
		op = ui_update();
		if (op == OP_EXIT)
			break;
		else if (op == OP_UPDATE)
			ui_draw();
	}

	transport_cleanup();
	ui_exit();
	gfxExit();
	return 0;
}
