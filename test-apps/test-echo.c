#if 0
/*
 * libwebsockets-test-echo
 *
 * Copyright (C) 2010-2016 Andy Green <andy@warmcat.com>
 *
 * This file is made available under the Creative Commons CC0 1.0
 * Universal Public Domain Dedication.
 *
 * The person who associated a work with this deed has dedicated
 * the work to the public domain by waiving all of his or her rights
 * to the work worldwide under copyright law, including all related
 * and neighboring rights, to the extent allowed by law. You can copy,
 * modify, distribute and perform the work, even for commercial purposes,
 * all without asking permission.
 *
 * The test apps are intended to be adapted for use in your code, which
 * may be proprietary.	So unlike the library itself, they are licensed
 * Public Domain.
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <assert.h>
#include <signal.h>

#include "../lib/libwebsockets.h"

#ifndef _WIN32
#include <syslog.h>
#include <sys/time.h>
#include <unistd.h>
#else
#include "gettimeofday.h"
#include <process.h>
#endif

static volatile int force_exit = 0;
static int versa, state;
static int times = -1;

#define LOCAL_RESOURCE_PATH INSTALL_DATADIR"/libwebsockets-test-server"

#define MAX_ECHO_PAYLOAD 1024

struct per_session_data__echo {
	size_t rx, tx;
	unsigned char buf[LWS_PRE + MAX_ECHO_PAYLOAD];
	unsigned int len;
	unsigned int index;
	int final;
	int continuation;
	int binary;
};

static int
callback_echo(struct lws *wsi, enum lws_callback_reasons reason, void *user,
	      void *in, size_t len)
{
	struct per_session_data__echo *pss =
			(struct per_session_data__echo *)user;
	int n, flags;

	switch (reason) {

#ifndef LWS_NO_SERVER

	case LWS_CALLBACK_ESTABLISHED:
		pss->index = 0;
		pss->len = -1;
		break;

	case LWS_CALLBACK_SERVER_WRITEABLE:
do_tx:
		if ((int)pss->len == -1)
			break;

		flags = lws_write_ws_flags(pss->binary ? LWS_WRITE_BINARY :
				LWS_WRITE_TEXT, pss->continuation, pss->final);

		lwsl_info("+++ test-echo: writing %d, with final %d\n",
			  pss->len, pss->final);

		pss->tx += pss->len;
		n = lws_write(wsi, &pss->buf[LWS_PRE], pss->len, flags);
		if (n < 0) {
			lwsl_err("ERROR %d writing to socket, hanging up\n", n);
			return 1;
		}
		pss->len = -1;
		if (pss->final)
			pss->continuation = 0;
		lws_rx_flow_control(wsi, 1);
		break;

	case LWS_CALLBACK_RECEIVE:
do_rx:
		pss->final = lws_is_final_fragment(wsi);
		pss->binary = lws_frame_is_binary(wsi);
		lwsl_info("+++ test-echo: RX len %ld final %ld, pss->len=%ld\n",
			  (long)len, (long)pss->final, (long)pss->len);

		memcpy(&pss->buf[LWS_PRE], in, len);
		assert((int)pss->len == -1);
		pss->len = (unsigned int)len;
		pss->rx += len;

		lws_rx_flow_control(wsi, 0);
		lws_callback_on_writable(wsi);
		break;
#endif

#ifndef LWS_NO_CLIENT
	/* when the callback is used for client operations --> */

	case LWS_CALLBACK_CLOSED:
	case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
		lwsl_debug("closed\n");
		state = 0;
		break;

	case LWS_CALLBACK_CLIENT_ESTABLISHED:
		lwsl_debug("Client has connected\n");
		pss->index = 0;
		pss->len = -1;
		state = 2;
		break;

	case LWS_CALLBACK_CLIENT_RECEIVE:
#ifndef LWS_NO_SERVER
		if (versa)
			goto do_rx;
#endif
		lwsl_notice("Client RX: %s", (char *)in);
		if (times == 0)
			force_exit = 1;
		break;

	case LWS_CALLBACK_CLIENT_WRITEABLE:
#ifndef LWS_NO_SERVER
		if (versa) {
			if (pss->len != (unsigned int)-1)
				goto do_tx;
			break;
		}
#endif
		/* we will send our packet... */
		pss->len = sprintf((char *)&pss->buf[LWS_PRE],
				   "hello from libwebsockets-test-echo client pid %d index %d\n",
				   getpid(), pss->index++);
		lwsl_notice("Client TX: %s", &pss->buf[LWS_PRE]);
		n = lws_write(wsi, &pss->buf[LWS_PRE], pss->len, LWS_WRITE_TEXT);
		if (n < 0) {
			lwsl_err("ERROR %d writing to socket, hanging up\n", n);
			return -1;
		}
		if (n < (int)pss->len) {
			lwsl_err("Partial write\n");
			return -1;
		}
		break;
#endif

	default:
		break;
	}

	return 0;
}



static struct lws_protocols protocols[] = {
	/* first protocol must always be HTTP handler */

	{
		"",		/* name - can be overridden with -e */
		callback_echo,
		sizeof(struct per_session_data__echo),	/* per_session_data_size */
		MAX_ECHO_PAYLOAD,
	},
	{
		NULL, NULL, 0		/* End of list */
	}
};

static const struct lws_extension exts[] = {
	{
		"permessage-deflate",
		lws_extension_callback_pm_deflate,
		"permessage-deflate; client_no_context_takeover; client_max_window_bits"
	},
	{ NULL, NULL, NULL /* terminator */ }
};


void sighandler(int sig)
{
	force_exit = 1;
}

static struct option options[] = {
	{ "help",	no_argument,		NULL, 'h' },
	{ "debug",	required_argument,	NULL, 'd' },
	{ "port",	required_argument,	NULL, 'p' },
	{ "ssl-cert",	required_argument,	NULL, 'C' },
	{ "ssl-key",	required_argument,	NULL, 'k' },
#ifndef LWS_NO_CLIENT
	{ "client",	required_argument,	NULL, 'c' },
	{ "ratems",	required_argument,	NULL, 'r' },
#endif
	{ "ssl",	no_argument,		NULL, 's' },
	{ "versa",	no_argument,		NULL, 'v' },
	{ "uri",	required_argument,	NULL, 'u' },
	{ "passphrase", required_argument,	NULL, 'P' },
	{ "interface",	required_argument,	NULL, 'i' },
	{ "times",	required_argument,	NULL, 'n' },
	{ "echogen",	no_argument,		NULL, 'e' },
#ifndef LWS_NO_DAEMONIZE
	{ "daemonize",	no_argument,		NULL, 'D' },
#endif
	{ NULL, 0, 0, 0 }
};

int main(int argc, char **argv)
{
	int n = 0;
	int port = 7681;
	int use_ssl = 0;
	struct lws_context *context;
	int opts = 0;
	char interface_name[128] = "";
	const char *_interface = NULL;
	char ssl_cert[256] = LOCAL_RESOURCE_PATH"/libwebsockets-test-server.pem";
	char ssl_key[256] = LOCAL_RESOURCE_PATH"/libwebsockets-test-server.key.pem";
#ifndef _WIN32
/* LOG_PERROR is not POSIX standard, and may not be portable */
#ifdef __sun
	int syslog_options = LOG_PID;
#else
	int syslog_options = LOG_PID | LOG_PERROR;
#endif
#endif
	int client = 0;
	int listen_port = 80;
	struct lws_context_creation_info info;
	char passphrase[256];
	char uri[256] = "/";
#ifndef LWS_NO_CLIENT
	char address[256], ads_port[256 + 30];
	int rate_us = 250000;
	unsigned long long oldus;
	struct lws *wsi;
	int disallow_selfsigned = 0;
	struct timeval tv;
	const char *connect_protocol = NULL;
	struct lws_client_connect_info i;
#endif

	int debug_level = 7;
#ifndef LWS_NO_DAEMONIZE
	int daemonize = 0;
#endif

	memset(&info, 0, sizeof info);

#ifndef LWS_NO_CLIENT
	lwsl_notice("Built to support client operations\n");
#endif
#ifndef LWS_NO_SERVER
	lwsl_notice("Built to support server operations\n");
#endif

	while (n >= 0) {
		n = getopt_long(argc, argv, "i:hsp:d:DC:k:P:vu:n:e"
#ifndef LWS_NO_CLIENT
			"c:r:"
#endif
				, options, NULL);
		if (n < 0)
			continue;
		switch (n) {
		case 'P':
			lws_strncpy(passphrase, optarg, sizeof(passphrase));
			info.ssl_private_key_password = passphrase;
			break;
		case 'C':
			lws_strncpy(ssl_cert, optarg, sizeof(ssl_cert));
			disallow_selfsigned = 1;
			break;
		case 'k':
			lws_strncpy(ssl_key, optarg, sizeof(ssl_key));
			break;
		case 'u':
			lws_strncpy(uri, optarg, sizeof(uri));
			break;

#ifndef LWS_NO_DAEMONIZE
		case 'D':
			daemonize = 1;
#if !defined(_WIN32) && !defined(__sun)
			syslog_options &= ~LOG_PERROR;
#endif
			break;
#endif
#ifndef LWS_NO_CLIENT
		case 'c':
			client = 1;
			lws_strncpy(address, optarg, sizeof(address));
			port = 80;
			break;
		case 'r':
			rate_us = atoi(optarg) * 1000;
			break;
#endif
		case 'd':
			debug_level = atoi(optarg);
			break;
		case 's':
			use_ssl = 1; /* 1 = take care about cert verification, 2 = allow anything */
			break;
		case 'p':
			port = atoi(optarg);
			break;
		case 'v':
			versa = 1;
			break;
		case 'e':
			protocols[0].name = "lws-echogen";
			connect_protocol = protocols[0].name;
			lwsl_err("using lws-echogen\n");
			break;
		case 'i':
			lws_strncpy(interface_name, optarg, sizeof interface_name);
			_interface = interface_name;
			break;
		case 'n':
			times = atoi(optarg) + 1;
			break;
		case '?':
		case 'h':
			fprintf(stderr, "Usage: libwebsockets-test-echo\n"
				"  --debug	/ -d <debug bitfield>\n"
				"  --port	/ -p <port>\n"
				"  --ssl-cert	/ -C <cert path>\n"
				"  --ssl-key	/ -k <key path>\n"
#ifndef LWS_NO_CLIENT
				"  --client	/ -c <server IP>\n"
				"  --ratems	/ -r <rate in ms>\n"
#endif
				"  --ssl	/ -s\n"
				"  --passphrase / -P <passphrase>\n"
				"  --interface	/ -i <interface>\n"
				"  --uri	/ -u <uri path>\n"
				"  --times	/ -n <-1 unlimited or times to echo>\n"
#ifndef LWS_NO_DAEMONIZE
				"  --daemonize	/ -D\n"
#endif
			);
			exit(1);
		}
	}

#ifndef LWS_NO_DAEMONIZE
	/*
	 * normally lock path would be /var/lock/lwsts or similar, to
	 * simplify getting started without having to take care about
	 * permissions or running as root, set to /tmp/.lwsts-lock
	 */
#if defined(WIN32) || defined(_WIN32)
#else
	if (!client && daemonize && lws_daemonize("/tmp/.lwstecho-lock")) {
		fprintf(stderr, "Failed to daemonize\n");
		return 1;
	}
#endif
#endif

#ifndef _WIN32
	/* we will only try to log things according to our debug_level */
	setlogmask(LOG_UPTO (LOG_DEBUG));
	openlog("lwsts", syslog_options, LOG_DAEMON);
#endif

	/* tell the library what debug level to emit and to send it to syslog */
	lws_set_log_level(debug_level, lwsl_emit_syslog);

	lwsl_notice("libwebsockets test server echo - license LGPL2.1+SLE\n");
	lwsl_notice("(C) Copyright 2010-2016 Andy Green <andy@warmcat.com>\n");

#ifndef LWS_NO_CLIENT
	if (client) {
		lwsl_notice("Running in client mode\n");
		listen_port = CONTEXT_PORT_NO_LISTEN;
		if (use_ssl && !disallow_selfsigned) {
			lwsl_info("allowing selfsigned\n");
			use_ssl = 2;
		} else {
			lwsl_info("requiring server cert validation against %s\n",
				  ssl_cert);
			info.ssl_ca_filepath = ssl_cert;
		}
	} else {
#endif
#ifndef LWS_NO_SERVER
		lwsl_notice("Running in server mode\n");
		listen_port = port;
#endif
#ifndef LWS_NO_CLIENT
	}
#endif

	info.port = listen_port;
	info.iface = _interface;
	info.protocols = protocols;
	if (use_ssl && !client) {
		info.ssl_cert_filepath = ssl_cert;
		info.ssl_private_key_filepath = ssl_key;
	} else
		if (use_ssl && client) {
			info.ssl_cert_filepath = NULL;
			info.ssl_private_key_filepath = NULL;
		}
	info.gid = -1;
	info.uid = -1;
	info.extensions = exts;
	info.options = opts | LWS_SERVER_OPTION_VALIDATE_UTF8;

	if (use_ssl)
		info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
#if !defined(LWS_WITHOUT_EXTENSIONS)
	info.extensions = exts;
#endif

	context = lws_create_context(&info);
	if (context == NULL) {
		lwsl_err("libwebsocket init failed\n");
		return -1;
	}


	signal(SIGINT, sighandler);

#ifndef LWS_NO_CLIENT
	gettimeofday(&tv, NULL);
	oldus = ((unsigned long long)tv.tv_sec * 1000000) + tv.tv_usec;
#endif

	n = 0;
	while (n >= 0 && !force_exit) {
#ifndef LWS_NO_CLIENT
		if (client && !state && times) {
			state = 1;
			lwsl_notice("Client connecting to %s:%u....\n",
				    address, port);
			/* we are in client mode */

			address[sizeof(address) - 1] = '\0';
			sprintf(ads_port, "%s:%u", address, port & 65535);
			if (times > 0)
				times--;

			memset(&i, 0, sizeof(i));

			i.context = context;
			i.address = address;
			i.port = port;
			i.ssl_connection = use_ssl;
			i.path = uri;
			i.host = ads_port;
			i.origin = ads_port;
			i.protocol = connect_protocol;

			wsi = lws_client_connect_via_info(&i);
			if (!wsi) {
				lwsl_err("Client failed to connect to %s:%u\n",
					 address, port);
				goto bail;
			}
		}

		if (client && !versa && times) {
			gettimeofday(&tv, NULL);

			if ((int)((((unsigned long long)tv.tv_sec * 1000000) + tv.tv_usec) - oldus) > rate_us) {
				lws_callback_on_writable_all_protocol(context,
						&protocols[0]);
				oldus = ((unsigned long long)tv.tv_sec * 1000000) + tv.tv_usec;
				if (times > 0)
					times--;
			}
		}

		if (client && !state && !times)
			break;
#endif
		n = lws_service(context, 10);
	}
#ifndef LWS_NO_CLIENT
bail:
#endif
	lws_context_destroy(context);

	lwsl_notice("libwebsockets-test-echo exited cleanly\n");
#ifndef _WIN32
	closelog();
#endif

	return 0;
}
#endif

#include <stdio.h>
#include <stdlib.h>
//#include <libwebsockets.h>
#include "../lib/libwebsockets.h"

static int callback_http(struct lws *wsi,
                         enum lws_callback_reasons reason, void *user,
                         void *in, size_t len)
{
	return 0;
}



static int callback_dumb_increment(struct lws *wsi,
                                   enum lws_callback_reasons reason,
                                   void *user, void *in, size_t len)
{
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED: // just log message that someone is connecting
            printf("connection established\n");
            break;
        case LWS_CALLBACK_RECEIVE: { // the funny part
            // create a buffer to hold our response
            // it has to have some pre and post padding. You don't need to care
            // what comes there, lwss will do everything for you. For more info see
            // http://git.warmcat.com/cgi-bin/cgit/lwss/tree/lib/lwss.h#n597
            unsigned char *buf = (unsigned char*) malloc(LWS_SEND_BUFFER_PRE_PADDING + len +
                                                         LWS_SEND_BUFFER_POST_PADDING);
            
            int i;
            
            // pointer to `void *in` holds the incomming request
            // we're just going to put it in reverse order and put it in `buf` with
            // correct offset. `len` holds length of the request.
            for (i=0; i < len; i++) {
                buf[LWS_SEND_BUFFER_PRE_PADDING + (len - 1) - i ] = ((char *) in)[i];
            }
            
            // log what we recieved and what we're going to send as a response.
            // that disco syntax `%.*s` is used to print just a part of our buffer
            // http://stackoverflow.com/questions/5189071/print-part-of-char-array
            printf("received data: %s, replying: %.*s\n", (char *) in, (int) len,
                   buf + LWS_SEND_BUFFER_PRE_PADDING);
            
            // send response
            // just notice that we have to tell where exactly our response starts. That's
            // why there's `buf[LWS_SEND_BUFFER_PRE_PADDING]` and how long it is.
            // we know that our response has the same length as request because
            // it's the same message in reverse order.
            lws_write(wsi, &buf[LWS_SEND_BUFFER_PRE_PADDING], len, LWS_WRITE_TEXT);
            
            // release memory back into the wild
            free(buf);
            break;
        }
        default:
            break;
    }
    
    return 0;
}



static struct lws_protocols protocols[] = {
    /* first protocol must always be HTTP handler */
    {
        "http-only",   // name
        callback_http, // callback
        0              // per_session_data_size
    },
    {
        "dumb-increment-protocol", // protocol name - very important!
        callback_dumb_increment,   // callback
        0                          // we don't use any per session data
    },
    {
        NULL, NULL, 0   /* End of list */
    }
};

int main(void) {
    // server url will be http://localhost:9000
    int port = 9000;
    struct lws_context *context;
    struct lws_context_creation_info context_info =
    {
        .port = port, .iface = NULL, .protocols = protocols, .extensions = NULL,
        .ssl_cert_filepath = NULL, .ssl_private_key_filepath = NULL, .ssl_ca_filepath = NULL,
        .gid = -1, .uid = -1, .options = 0, NULL, .ka_time = 0, .ka_probes = 0, .ka_interval = 0
    };
    // create lws context representing this server
    context = lws_create_context(&context_info);

    if (context == NULL) {
        fprintf(stderr, "lws init failed\n");
        return -1;
    }
    
    printf("starting server...\n");
    
    // infinite loop, to end this server send SIGTERM. (CTRL+C)
    while (1) {
        lws_service(context, 50);
        // lws_service will process all waiting events with their
        // callback functions and then wait 50 ms.
        // (this is a single threaded webserver and this will keep our server
        // from generating load while there are not requests to process)
    }
    
    lws_context_destroy(context);
    
    return 0;
}