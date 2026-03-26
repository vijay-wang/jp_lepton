// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * main.c - Server entry point.
 */

#include <stdio.h>
#include <signal.h>
#include "server.h"
#include "log.h"

static server_handle_t *g_srv;

static void on_signal(int sig)
{
	(void)sig;
	pr_info("\n[server] Shutting down...\n");
	server_stop(g_srv);
}

int main(void)
{
	server_config_t cfg = (server_config_t)SERVER_CONFIG_DEFAULT;
	int ret;

	signal(SIGINT,  on_signal);
	signal(SIGTERM, on_signal);
	signal(SIGPIPE, SIG_IGN);

	g_srv = server_create(&cfg);
	if (!g_srv) {
		pr_err("[server] create failed\n");
		return 1;
	}

	ret = server_run(g_srv);
	server_destroy(g_srv);
	return ret;
}
