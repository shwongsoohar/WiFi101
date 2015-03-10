/**
 *
 * \file
 *
 * \brief BSD compatible socket interface.
 *
 * Copyright (c) 2014 Atmel Corporation. All rights reserved.
 *
 * \asf_license_start
 *
 * \page License
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. The name of Atmel may not be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * 4. This software may only be redistributed and used in connection with an
 *    Atmel microcontroller product.
 *
 * THIS SOFTWARE IS PROVIDED BY ATMEL "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
 * EXPRESSLY AND SPECIFICALLY DISCLAIMED. IN NO EVENT SHALL ATMEL BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * \asf_license_stop
 *
 */

#include <string.h>
#include "socket/include/socket.h"
#include "driver/source/m2m_hif.h"
#include "socket/source/socket_internal.h"
#include "socket/include/socket_buffer.h"

tstrSocketBuffer gastrSocketBuffer[MAX_SOCKET];

void socketBufferInit(void)
{
	memset(gastrSocketBuffer, 0, sizeof(gastrSocketBuffer));
}

void socketBufferRegister(SOCKET socket, uint32 *flag, uint32 *head, uint32 *tail, uint8 *buffer)
{
	gastrSocketBuffer[socket].flag = flag;
	gastrSocketBuffer[socket].head = head;
	gastrSocketBuffer[socket].tail = tail;
	gastrSocketBuffer[socket].buffer = buffer;
}

void socketBufferUnregister(SOCKET socket)
{
	gastrSocketBuffer[socket].flag = 0;
	gastrSocketBuffer[socket].head = 0;
	gastrSocketBuffer[socket].tail = 0;
	gastrSocketBuffer[socket].buffer = 0;
}

void socketBufferCb(SOCKET sock, uint8 u8Msg, void *pvMsg)
{
	switch (u8Msg) {
		/* Socket connected. */
		case SOCKET_MSG_CONNECT:
		{
			tstrSocketConnectMsg *pstrConnect = (tstrSocketConnectMsg *)pvMsg;
			if (pstrConnect && pstrConnect->s8Error >= 0) {
				recv(sock, gastrSocketBuffer[sock].buffer, SOCKET_BUFFER_MTU, 0);
				*(gastrSocketBuffer[sock].flag) |= SOCKET_BUFFER_FLAG_CONNECTED;
			} else {
				close(sock);
			}
		}
		break;
		
		/* TCP Data receive. */
		case SOCKET_MSG_RECV:
		{
			tstrSocketRecvMsg *pstrRecv = (tstrSocketRecvMsg *)pvMsg;
			if (pstrRecv && pstrRecv->s16BufferSize > 0) {
				/* Data received. */
				*(gastrSocketBuffer[sock].head) += pstrRecv->s16BufferSize;
				
				/* Buffer full, stop reception. */
				if (SOCKET_BUFFER_TCP_SIZE - *(gastrSocketBuffer[sock].head) < SOCKET_BUFFER_MTU) {
					*(gastrSocketBuffer[sock].flag) |= SOCKET_BUFFER_FLAG_FULL;
				}
				else {
					recv(sock, gastrSocketBuffer[sock].buffer + *(gastrSocketBuffer[sock].head),
						SOCKET_BUFFER_MTU, 0);
				}
			}
			/* Test EOF (Socket closed) condition for TCP socket. */
			else {
				*(gastrSocketBuffer[sock].flag) &= ~SOCKET_BUFFER_FLAG_CONNECTED;
				close(sock);
			}
		}
		break;

		/* UDP Data receive. */
		case SOCKET_MSG_RECVFROM:
		{
			tstrSocketRecvMsg *pstrRecv = (tstrSocketRecvMsg *)pvMsg;
			if (pstrRecv && pstrRecv->s16BufferSize > 0) {
				uint32 h = *(gastrSocketBuffer[sock].head);
				uint8 *buf = gastrSocketBuffer[sock].buffer;
				
				/* Store packet size. */
				buf[h++] = pstrRecv->s16BufferSize >> 8;
				buf[h++] = pstrRecv->s16BufferSize;

				/* Store remote host port. */
				buf[h++] = pstrRecv->strRemoteAddr.sin_port;
				buf[h++] = pstrRecv->strRemoteAddr.sin_port >> 8;

				/* Store remote host IP. */
				buf[h++] = pstrRecv->strRemoteAddr.sin_addr.s_addr >> 24;
				buf[h++] = pstrRecv->strRemoteAddr.sin_addr.s_addr >> 16;
				buf[h++] = pstrRecv->strRemoteAddr.sin_addr.s_addr >> 8;
				buf[h++] = pstrRecv->strRemoteAddr.sin_addr.s_addr;
				
				/* Data received. */
				*(gastrSocketBuffer[sock].head) = h + pstrRecv->s16BufferSize;
				
				/* Buffer full, stop reception. */
				if (SOCKET_BUFFER_UDP_SIZE - *(gastrSocketBuffer[sock].head) < SOCKET_BUFFER_MTU + SOCKET_BUFFER_UDP_HEADER_SIZE) {
					*(gastrSocketBuffer[sock].flag) |= SOCKET_BUFFER_FLAG_FULL;
				}
				else {
					recvfrom(sock, gastrSocketBuffer[sock].buffer + *(gastrSocketBuffer[sock].head) + SOCKET_BUFFER_UDP_HEADER_SIZE,
							SOCKET_BUFFER_MTU, 0);
				}
			}
		}
		break;

		/* Socket bind. */
		case SOCKET_MSG_BIND:
		{
			tstrSocketBindMsg *pstrBind = (tstrSocketBindMsg *)pvMsg;
			if (pstrBind && pstrBind->status == 0) {
				*(gastrSocketBuffer[sock].flag) |= SOCKET_BUFFER_FLAG_BIND;
				/* TCP socket needs to enter Listen state. */
				if (sock < TCP_SOCK_MAX) {
					listen(sock, 0);
				}
				/* UDP socket only needs to supply the receive buffer. */
				/* +8 is used to store size, port and IP of incoming data. */
				else {
					recvfrom(sock, gastrSocketBuffer[sock].buffer + SOCKET_BUFFER_UDP_HEADER_SIZE,
							SOCKET_BUFFER_MTU, 0);
				}
			}
		}
		break;

		/* Connect accept. */
		case SOCKET_MSG_ACCEPT:
		{
			tstrSocketAcceptMsg *pstrAccept = (tstrSocketAcceptMsg *)pvMsg;
			if (pstrAccept) {
				if (*(gastrSocketBuffer[sock].flag) & SOCKET_BUFFER_FLAG_SPAWN) {
					/* One spawn connection already waiting, discard current one. */
					close(pstrAccept->sock);
				}
				else {
					/* Use flag to store spawn TCP descriptor. */
					*(gastrSocketBuffer[sock].flag) &= ~SOCKET_BUFFER_SERVER_SOCKET_MSK;
					*(gastrSocketBuffer[sock].flag) |= pstrAccept->sock;
					*(gastrSocketBuffer[sock].flag) |= SOCKET_BUFFER_FLAG_SPAWN;
				}
			}
		}
		break;
	
	}
}
