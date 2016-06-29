/**
 * Copyright (c) 2001-2004 Swedish Institute of Computer Science.
 * All rights reserved. 
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
 * This file is part of and a contribution to the lwIP TCP/IP stack.
 *
 * Credits go to Adam Dunkels (and the current maintainers) of this software.
 *
 * Christiaan Simons rewrote this file to get a more stable echo example.
 *
 **/

#include <string.h>
#include "stm32f4_discovery.h"

#include "lwip/debug.h"
#include "lwip/stats.h"
#include "lwip/tcp.h"

#include "dds.h"
#include "dds_server.h"

/* DDS server protocol states */
enum tcp_echoserver_states
{
	DS_IDLE = 0,		/* idle, waiting for connection */
	DS_HEADER,			/* waiting for frame header */
	DS_RECEIVING,		/* receiving data */
};

/* DDS server state */
struct dds_server_struct
{
	u8_t 				state;      /* current protocol state */

	union {
		dds_header 		*header;	/* DDS frame header */
		unsigned char	*data;		/* DDS data*/
	} dds;

	size_t 				recv_size;  /* size of DDS data in buffer*/
	size_t				max_size;	/* DDS buffer size */
};

static struct tcp_pcb *dds_server_pcb;
static struct dds_server_struct dds_server_state;

/* LEDs */
#define DDS_SERVER_LED_DATA_ERROR           (LED3)		/* orange */
#define DDS_SERVER_LED_CONVERSION			(LED4)		/* green */
#define DDS_SERVER_LED_PROTOCOL_ERROR   	(LED5)		/* red */
#define DDS_SERVER_LED_ACTIVE_CONNECTION  	(LED6)		/* blue */

static void dds_server_error(void *arg, err_t err)
{
	STM_EVAL_LEDOn(DDS_SERVER_LED_PROTOCOL_ERROR);
}

static void dds_server_connection_close(struct tcp_pcb *tpcb, struct dds_server_struct *dds_server)
{
	/* remove all callbacks */
	tcp_arg(tpcb, NULL);
	tcp_sent(tpcb, NULL);
	tcp_recv(tpcb, NULL);
	tcp_err(tpcb, NULL);
	tcp_poll(tpcb, NULL, 0);

	STM_EVAL_LEDOff(DDS_SERVER_LED_ACTIVE_CONNECTION);

	dds_server->state = DS_IDLE;

	/* close tcp connection */
	tcp_close(tpcb);
}

static err_t dds_server_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
	LWIP_ASSERT("arg != NULL", arg != NULL);
	LWIP_UNUSED_ARG(len);

	struct dds_server_struct *dds_server = arg;

	dds_server_connection_close(tpcb, dds_server);

	return ERR_OK;
}

static void dds_server_send(struct tcp_pcb *tpcb, struct dds_server_struct *dds_server, enum dds_res res)
{
	err_t wr_err = ERR_OK;

	const char *res_str = dds_res_to_str(res);

	tcp_sent(tpcb, dds_server_sent);
	wr_err = tcp_write(tpcb, res_str, strlen(res_str), 1);
	if (wr_err == ERR_OK)
		tcp_output(tpcb);
	else
		dds_server_connection_close(tpcb, dds_server);
}

static err_t dds_server_poll(void *arg, struct tcp_pcb *tpcb)
{
	LWIP_ASSERT("arg != NULL", arg != NULL);

	struct dds_server_struct *dds_server = arg;

	dds_server_send(tpcb, dds_server, DDS_ERR_TIMEOUT);

	return ERR_OK;
}

static err_t dds_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
	LWIP_ASSERT("arg != NULL", arg != NULL);

	struct dds_server_struct *dds_server = arg;

	if (p == NULL) {
		/* remote host closed connection */
		dds_server_connection_close(tpcb, dds_server);
		return ERR_OK;
	}

	if (err != ERR_OK) {
		dds_server->state = DS_IDLE;

		STM_EVAL_LEDOff(DDS_SERVER_LED_ACTIVE_CONNECTION);
		STM_EVAL_LEDOn(DDS_SERVER_LED_PROTOCOL_ERROR);

		return err;
	}

	if (dds_server->state == DS_HEADER) {
		if (!dds_verify_header(p->payload)) {
			dds_server_send(tpcb, dds_server, DDS_ERR_HEADER);
			return ERR_OK;
		}
		dds_server->state = DS_RECEIVING;
	}

	size_t copy_len = p->len;
	if (dds_server->recv_size + copy_len > dds_server->max_size) {
		/* no enough memory */
		dds_server_send(tpcb, dds_server, DDS_ERR_MEM);
		STM_EVAL_LEDOn(DDS_SERVER_LED_PROTOCOL_ERROR);

		return ERR_OK;
	}

	MEMCPY(dds_server->dds.data + dds_server->recv_size, p->payload, copy_len);
	dds_server->recv_size += copy_len;
	tcp_recved(tpcb, p->len);

	/* check if received whole data */
	if ((dds_server->recv_size >= sizeof(struct dds_header_struct)) &&
			(dds_server->dds.header->size <= dds_server->recv_size)) {
		STM_EVAL_LEDOff(DDS_SERVER_LED_CONVERSION);
		dds_res res = DDS_Start(dds_server->dds.header);
		if (res != DDS_OK)
			STM_EVAL_LEDOn(DDS_SERVER_LED_DATA_ERROR);

		dds_server_send(tpcb, dds_server, res);
	}

	return ERR_OK;
}

static err_t dds_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
	LWIP_ASSERT("arg != NULL", arg != NULL);
	LWIP_UNUSED_ARG(err);

	struct dds_server_struct *dds_server = arg;

	if (dds_server->state != DS_IDLE) {
		/* only one active connection is permitted */
		return ERR_INPROGRESS;
	}

	STM_EVAL_LEDOn(DDS_SERVER_LED_ACTIVE_CONNECTION);
	STM_EVAL_LEDOff(DDS_SERVER_LED_PROTOCOL_ERROR);
	STM_EVAL_LEDOff(DDS_SERVER_LED_DATA_ERROR);

	DDS_Stop();
	STM_EVAL_LEDOff(DDS_SERVER_LED_CONVERSION);

	dds_server->state = DS_HEADER;
	dds_server->recv_size = 0;

	tcp_setprio(newpcb, TCP_PRIO_MIN);

    tcp_arg(newpcb, dds_server);

    tcp_recv(newpcb, dds_server_recv);
    tcp_err(newpcb, dds_server_error);
    tcp_poll(newpcb, dds_server_poll, 60);

    return ERR_OK;
}

static void dds_server_toggle_conversion_led()
{
	STM_EVAL_LEDToggle(DDS_SERVER_LED_CONVERSION);
}

static void dds_server_dds_error_led()
{
	STM_EVAL_LEDOn(DDS_SERVER_LED_DATA_ERROR);
}

void dds_server_init(void)
{
	dds dds_init;
	dds_server_state.max_size = 1024;

	/* allocate DDS data buffer */
	dds_server_state.dds.data = mem_malloc(dds_server_state.max_size);
	if (!dds_server_state.dds.data) {
		printf("Can not allocate memory for DDS data\n");
		return;
	}

	/* initialize LEDs*/
	STM_EVAL_LEDInit(DDS_SERVER_LED_DATA_ERROR);
	STM_EVAL_LEDInit(DDS_SERVER_LED_CONVERSION);
	STM_EVAL_LEDInit(DDS_SERVER_LED_PROTOCOL_ERROR);
	STM_EVAL_LEDInit(DDS_SERVER_LED_ACTIVE_CONNECTION);

	/* initialize DDS functionality */
	dds_init.dds_sync = dds_server_toggle_conversion_led;
	dds_init.dds_err  = dds_server_dds_error_led;
	DDS_Init(dds_init);

	/* create new tcp pcb */
	dds_server_pcb = tcp_new();

	if (!dds_server_pcb) {
		printf("Can not create new pcb\n");
		return;
	}

	/* bind to port 1234 */
	err_t err = tcp_bind(dds_server_pcb, IP_ADDR_ANY, 1234);

    if (err != ERR_OK) {
    	printf("Can not bind pcb\n");
    	return;
    }

    /* start tcp listening */
    dds_server_pcb = tcp_listen(dds_server_pcb);

    tcp_arg(dds_server_pcb, &dds_server_state);

    dds_server_state.state = DS_IDLE;

    /* initialize LwIP tcp_accept callback function */
    tcp_accept(dds_server_pcb, dds_server_accept);
}
