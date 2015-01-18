/* Finit TTY handling
 *
 * Copyright (c) 2013  Mattias Walström <lazzer@gmail.com>
 * Copyright (c) 2013  Joachim Nilsson <troglobit@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <signal.h>
#include <sys/wait.h>

#include "finit.h"
#include "helpers.h"
#include "lite.h"
#include "tty.h"

LIST_HEAD(, tty_node) tty_list = LIST_HEAD_INITIALIZER();


/* tty [!1-9,S] <DEV> [BAUD[,BAUD,...]] [TERM] */
int tty_register(char *line)
{
	tty_node_t *entry;
	int         insert = 0, baud = 0;
	char       *cmd, *dev;
	char       *runlevels = NULL, *term = NULL;

	if (!line) {
		_e("Invalid input argument.");
		return errno = EINVAL;
	}

	cmd = strtok(line, " ");
	if (!cmd) {
	incomplete:
		_e("Incomplete tty, cannot register.");
		return errno = EINVAL;
	}

	if (cmd[0] == '[') {	/* [runlevels] */
		runlevels = &cmd[0];
		dev = strtok(NULL, " ");
		if (!dev)
			goto incomplete;
	} else {
		dev = cmd;
	}

	cmd = strtok(NULL, " ");
	if (cmd) {
		baud = strtonum(cmd, 50, 4000000, NULL);
		term = strtok(NULL, " ");
	}

	entry = tty_find(dev);
	if (!entry) {
		insert = 1;
		entry = calloc(1, sizeof(*entry));
		if (!entry)
			return errno = ENOMEM;
	}

	entry->data.name = strdup(dev);
	if (!baud)
		baud = BAUDRATE;
	entry->data.baud = baud;
	entry->data.term = term ? strdup(term) : NULL;
	entry->data.runlevels = parse_runlevels(runlevels);
	_d("Registering tty %s at %d baud with term=%s on runlevels %s", dev, baud, term ?: "N/A", runlevels);

	if (insert)
		LIST_INSERT_HEAD(&tty_list, entry, link);

	return 0;
}

tty_node_t *tty_find(char *dev)
{
	tty_node_t *entry;

	LIST_FOREACH(entry, &tty_list, link) {
		if (!strcmp(dev, entry->data.name))
			return entry;
	}

	return NULL;
}

size_t tty_num(void)
{
	size_t num = 0;
	tty_node_t *entry;

	LIST_FOREACH(entry, &tty_list, link)
		num++;

	return num;
}

tty_node_t *tty_find_by_pid(pid_t pid)
{
	tty_node_t *entry;

	LIST_FOREACH(entry, &tty_list, link) {
		if (entry->data.pid == pid)
			return entry;
	}

	return NULL;
}

void tty_start(finit_tty_t *tty)
{
	int i = 0, is_console = 0;
	char *cmd, *arg;
	char  line[FILENAME_MAX];
	char *args[42];

	if (tty->pid)
		return;

	if (console && !strcmp(tty->name, console))
		is_console = 1;

	if (!strncmp(tty->name, _PATH_DEV, strlen(_PATH_DEV))) {
#if   defined(GETTY_AGETTY)
		snprintf(line, sizeof(line), "%s %s %d %s", GETTY, tty->name, tty->baud, tty->term ?: "");
#elif defined(GETTY_BUSYBOX)
		snprintf(line, sizeof(line), "%s %d %s %s", GETTY, tty->baud, tty->name, tty->term ?: "");
#else
		strlcpy(line, FALLBACK_SHELL, sizeof(line));
#endif
	} else {
		strlcpy(line, tty->name, sizeof(line));
	}
	cmd = strtok(line, " ");
	args[i++] = basename(cmd);
	while ((arg = strtok(NULL, " ")))
		args[i++] = arg;
	args[i] = NULL;

	_d("Starting %s: %s on %s", is_console ? "console" : "TTY", cmd, tty->name);
	tty->pid = run_getty(cmd, args, is_console);
}

void tty_stop(finit_tty_t *tty)
{
	if (!tty->pid)
		return;

	_d("Stopping TTY %s", tty->name);
	kill(tty->pid, SIGTERM);
	do_sleep(2);
	kill(tty->pid, SIGKILL);
	waitpid(tty->pid, NULL, 0);
	tty->pid = 0;
}

int tty_enabled(finit_tty_t *tty, int runlevel)
{
	if (!tty)
		return 0;

	if (!ISSET(tty->runlevels, runlevel))
		return 0;
	else if (fexist(tty->name))
		return 1;

	return 0;
}

/* TTY monitor, called by svc_monitor() */
int tty_respawn(pid_t pid)
{
	tty_node_t *entry = tty_find_by_pid(pid);

	if (!entry)
		return 0;

	/* Clear PID to be able to respawn it. */
	entry->data.pid = 0;

	if (!tty_enabled(&entry->data, runlevel))
		tty_stop(&entry->data);
	else
		tty_start(&entry->data);

	return 1;
}

/* Start all TTYs that exist in the system and are allowed at this runlevel */
void tty_runlevel(int runlevel)
{
	tty_node_t *entry;

	LIST_FOREACH(entry, &tty_list, link) {
		if (!tty_enabled(&entry->data, runlevel))
			tty_stop(&entry->data);
		else
			tty_start(&entry->data);
	}
}

/**
 * Local Variables:
 *  version-control: t
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
