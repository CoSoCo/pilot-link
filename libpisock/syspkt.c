/*
 * syspkt.c:  Pilot SysPkt manager
 *
 * (c) 1996, Kenneth Albanowski.
 * Derived from padp.c.
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Library General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef WIN32
#include <winsock.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "pi-source.h"
#include "pi-syspkt.h"
#include "pi-slp.h"
#include "pi-serial.h"

/* Declare prototypes */
int sys_RPCerror;
int sys_PackRegisters(void *data, struct Pilot_registers *r);
int RPC_MemCardInfo(int sd, int cardno, char * cardname, char * manufname,
                    int * version, long * date, long * romsize, long * ramsize,
                    long * freeram);


/***********************************************************************
 *
 * Function:    sys_UnpackState
 *
 * Summary:     Get the state command
 *
 * Parameters:  None
 *
 * Returns:     Nothing
 *
 ***********************************************************************/
int
sys_UnpackState(void *buffer, struct Pilot_state *s)
{
	int 	idx;
	unsigned char *data = buffer;

	s->reset 		= get_short(data);
	s->exception 		= get_short(data + 2);
	memcpy(s->func_name, data + 152, 32);
	memcpy(s->instructions, data + 78, 30);
	s->func_name[32 - 1] 	= 0;
	s->func_start 		= get_long(data + 144);
	s->func_end 		= get_long(data + 148);
	sys_UnpackRegisters(data + 4, &s->regs);

	for (idx = 0; idx < 6; idx++) {
		s->breakpoint[idx].address = get_long(data + 108 + idx * 6);
		s->breakpoint[idx].enabled = get_byte(data + 112 + idx * 6);
	}

	s->trap_rev = get_short(data + 184);

	return 0;
}


/***********************************************************************
 *
 * Function:    sys_UnpackRegisters
 *
 * Summary:     Read the register commands
 *
 * Parameters:  None
 *
 * Returns:     0
 *
 ***********************************************************************/
int
sys_UnpackRegisters(void *data, struct Pilot_registers *r)
{
	unsigned char *buffer = data;

	r->D[0] = get_long(buffer + 0);
	r->D[1] = get_long(buffer + 4);
	r->D[2] = get_long(buffer + 8);
	r->D[3] = get_long(buffer + 12);
	r->D[4] = get_long(buffer + 16);
	r->D[5] = get_long(buffer + 20);
	r->D[6] = get_long(buffer + 24);
	r->D[7] = get_long(buffer + 28);
	r->A[0] = get_long(buffer + 32);
	r->A[1] = get_long(buffer + 36);
	r->A[2] = get_long(buffer + 40);
	r->A[3] = get_long(buffer + 44);
	r->A[4] = get_long(buffer + 48);
	r->A[5] = get_long(buffer + 52);
	r->A[6] = get_long(buffer + 56);
	r->USP  = get_long(buffer + 60);
	r->SSP  = get_long(buffer + 64);
	r->PC   = get_long(buffer + 68);

	r->SR   = get_short(buffer + 72);

	return 0;
}


/***********************************************************************
 *
 * Function:    sys_PackRegisters
 *
 * Summary:     Pack the register commands
 *
 * Parameters:  None
 *
 * Returns:     0
 *
 ***********************************************************************/
int
sys_PackRegisters(void *data, struct Pilot_registers *r)
{
	int 	idx;
	unsigned char *buffer = data;

	for (idx = 0; idx < 8; idx++)
		set_long(buffer + idx * 4, r->D[idx]);
	for (idx = 0; idx < 7; idx++)
		set_long(buffer + 32 + idx * 4, r->A[idx]);
	set_long(buffer + 60, r->USP);
	set_long(buffer + 64, r->SSP);
	set_long(buffer + 68, r->PC);

	set_short(buffer + 72, r->SR);

	return 0;
}

/***********************************************************************
 *
 * Function:    sys_Continue
 *
 * Summary:     Define the Continue command
 *
 * Parameters:  None
 *
 * Returns:     Nothing
 *
 ***********************************************************************/
int
sys_Continue(int sd, struct Pilot_registers *r, struct Pilot_watch *w)
{
	char 	buf[94];

	buf[0] 	= 0;
	buf[1] 	= 0;
	buf[2] 	= 0;
	buf[3] 	= 0;
	buf[4] 	= 0x07;
	buf[5] 	= 0;		/* gapfill */

	if (!r)
		return pi_write(sd, buf, 6);

	sys_PackRegisters(buf + 6, r);
	set_byte(buf + 80, (w != 0) ? 1 : 0);
	set_byte(buf + 81, 0);
	set_long(buf + 82, w ? w->address : 0);
	set_long(buf + 86, w ? w->length : 0);
	set_long(buf + 90, w ? w->checksum : 0);

	return pi_write(sd, buf, 94);
}

/***********************************************************************
 *
 * Function:    sys_Step
 *
 * Summary:     Single-step command
 *
 * Parameters:  None
 *
 * Returns:     Socket, command, 6 bytes
 *
 ***********************************************************************/
int
sys_Step(int sd)
{
	char 	buf[94];

	buf[0] = 0;
	buf[1] = 0;
	buf[2] = 0;
	buf[3] = 0;
	buf[4] = 0x03;
	buf[5] = 0;		/* gapfill */

	return pi_write(sd, buf, 6);
}

/***********************************************************************
 *
 * Function:    sys_SetBreakpoints
 *
 * Summary:     Set the breakpoints (0x0C, 0x8C)
 *
 * Parameters:  None
 *
 * Returns:     Nothing
 *
 ***********************************************************************/
int
sys_SetBreakpoints(int sd, struct Pilot_breakpoint *b)
{
	int 	idx;
	char 	buf[94];

	buf[0] = 0;
	buf[1] = 0;
	buf[2] = 0;
	buf[3] = 0;
	buf[4] = 0x0c;
	buf[5] = 0;		/* gapfill */

	for (idx = 0; idx < 6; idx++) {
		set_long(buf + 6 + idx * 6, b[idx].address);
		set_byte(buf + 10 + idx * 6, b[idx].enabled);
		set_byte(buf + 11 + idx * 6, 0);
	}

	pi_write(sd, buf, 42);

	idx = pi_read(sd, buf, 6);

	if ((idx <= 0) || (buf[4] != (char) 0x8c))
		return 0;
	else
		return 1;
}

/***********************************************************************
 *
 * Function:    sys_SetTrapBreaks
 *
 * Summary:     Set the Trap Breaks (0x11, 0x91)
 *
 * Parameters:  None
 *
 * Returns:     Nothing
 *
 ***********************************************************************/
int
sys_SetTrapBreaks(int sd, int *traps)
{
	int 	idx;
	char 	buf[94];

	buf[0] = 0;
	buf[1] = 0;
	buf[2] = 0;
	buf[3] = 0;
	buf[4] = 0x11;
	buf[5] = 0;		/* gapfill */

	for (idx = 0; idx < 5; idx++) {
		set_short(buf + 6 + idx * 2, traps[idx]);
	}

	pi_write(sd, buf, 16);

	idx = pi_read(sd, buf, 6);

	if ((idx <= 0) || (buf[4] != (char) 0x91))
		return 0;
	else
		return 1;
}

/***********************************************************************
 *
 * Function:    sys_GetTrapBreaks
 *
 * Summary:     Get the Trap Breaks (0x10, 0x90)
 *
 * Parameters:  None
 *
 * Returns:     Nothing
 *
 ***********************************************************************/
int
sys_GetTrapBreaks(int sd, int *traps)
{
	int 	idx;
	char 	buf[94];

	buf[0] = 0;
	buf[1] = 0;
	buf[2] = 0;
	buf[3] = 0;
	buf[4] = 0x10;
	buf[5] = 0;		/* gapfill */

	pi_write(sd, buf, 6);

	idx = pi_read(sd, buf, 16);

	if ((idx < 16) || (buf[4] != (char) 0x90))
		return 0;

	for (idx = 0; idx < 5; idx++) {
		traps[idx] = get_short(buf + 6 + idx * 2);
	}

	return 1;
}


/***********************************************************************
 *
 * Function:    sys_ToggleDbgBreaks
 *
 * Summary:     Enable the DbgBreaks command (0x0D, 0x8D)
 *
 * Parameters:  None
 *
 * Returns:     Nothing
 *
 ***********************************************************************/
int
sys_ToggleDbgBreaks(int sd)
{
	int 	idx;
	char 	buf[94];

	buf[0] = 0;
	buf[1] = 0;
	buf[2] = 0;
	buf[3] = 0;
	buf[4] = 0x0d;
	buf[5] = 0;		/* gapfill */

	pi_write(sd, buf, 6);

	idx = pi_read(sd, buf, 7);

	if ((idx < 7) || (buf[4] != (char) 0x8d))
		return 0;

	return get_byte(buf + 6);
}


/***********************************************************************
 *
 * Function:    sys_QueryState
 *
 * Summary:     Query the state (uh)
 *
 * Parameters:  None
 *
 * Returns:     Nothing
 *
 ***********************************************************************/
int
sys_QueryState(int sd)
{
	char 	buf[6];

	buf[0] = 0;
	buf[1] = 0;
	buf[2] = 0;
	buf[3] = 0;
	buf[4] = 0;
	buf[5] = 0;		/* gapfill */

	return pi_write(sd, buf, 2);
}

/***********************************************************************
 *
 * Function:    sys_ReadMemory
 *
 * Summary:     Read memory (0x01, 0x81)
 *
 * Parameters:  None
 *
 * Returns:     Nothing
 *
 ***********************************************************************/
int
sys_ReadMemory(int sd, unsigned long addr, unsigned long len, void *dest)
{
	int 	result;
	unsigned char buf[0xffff];
	unsigned long todo, done;

	done = 0;
	do {
		todo 	= len;
		if (todo > 256)
			todo = 256;

		buf[0] = 0;
		buf[1] = 0;
		buf[2] = 0;
		buf[3] = 0;
		buf[4] = 0x01;
		buf[5] = 0;	/* gapfill */

		set_long(buf + 6, addr + done);
		set_short(buf + 10, todo);

		pi_write(sd, buf, 12);

		result = pi_read(sd, buf, todo + 6);

		if (result < 0)
			return done;

		if ((buf[4] == 0x81)
		    && ((unsigned int) result == todo + 6)) {
			memcpy(((char *) dest) + done, buf + 6, todo);
			done += todo;
		} else {
			return done;
		}
	} while (done < len);
	return done;
}


/***********************************************************************
 *
 * Function:    sys_WriteMemory
 *
 * Summary:     Write memory (0x02, 0x82) 
 *
 * Parameters:  None
 *
 * Returns:     Nothing
 *
 ***********************************************************************/
int
sys_WriteMemory(int sd, unsigned long addr, unsigned long len, void *src)
{
	int 	result;
	unsigned char buf[0xffff];
	unsigned long todo, done;

	done = 0;
	do {
		todo = len;
		if (todo > 256)
			todo = 256;

		buf[0] = 0;
		buf[1] = 0;
		buf[2] = 0;
		buf[3] = 0;
		buf[4] = 0x02;
		buf[5] = 0;	/* gapfill */

		set_long(buf + 6, addr);
		set_short(buf + 10, len);
		memcpy(buf + 12, ((char *) src) + done, todo);

		pi_write(sd, buf, len + 12);

		result = pi_read(sd, buf, 6);

		if (result < 0)
			return done;

		if ((buf[4] == 0x82)
		    && ((unsigned int) result == todo + 6)) {
			;
		} else {
			return done;
		}
	} while (done < len);
	return done;
}


/***********************************************************************
 *
 * Function:    sys_Find
 *
 * Summary:     Searches a range of addresses for data (0x13, 0x80)
 *
 * Parameters:  None
 *
 * Returns:     Nothing
 *
 ***********************************************************************/
int
sys_Find(int sd, unsigned long startaddr, unsigned long stopaddr, size_t len,
	 int caseinsensitive, void *data, unsigned long *found)
{
	int 	result;
	unsigned char buf[0xffff];

	buf[0] = 0;
	buf[1] = 0;
	buf[2] = 0;
	buf[3] = 0;
	buf[4] = 0x11;
	buf[5] = 0;		/* gapfill */

	set_long(buf + 6, startaddr);
	set_long(buf + 10, stopaddr);
	set_short(buf + 14, len);
	set_byte(buf + 16, caseinsensitive);
	memcpy(buf + 17, data, len);

	pi_write(sd, buf, len + 17);

	result = pi_read(sd, buf, 12);

	if (result < 0)
		return result;

	if (found)
		*found = get_long(buf + 6);

	return get_byte(buf + 10);
}


/***********************************************************************
 *
 * Function:    sys_RemoteEvent
 *
 * Summary:     Parameters sent from host to target to feed pen and 
 *		keyboard events. They do not require a response.
 *
 * Parameters:  None
 *
 * Returns:     Nothing
 *
 ***********************************************************************/
int
sys_RemoteEvent(int sd, int penDown, int x, int y, int keypressed,
		int keymod, int keyasc, int keycode)
{
	char 	buf[16];

	/* Offset 1, 3 and 9 are padding */
	set_byte(&buf[0], 0x0D); /* RemoteEvtCommand	*/
	set_byte(&buf[1], 0);
	set_byte(&buf[2], penDown);
	set_byte(&buf[3], 0);
	set_short(&buf[4], x);
	set_short(&buf[6], y);
	set_byte(&buf[8], keypressed);
	set_byte(&buf[9], 0);
	set_short(&buf[10], keymod);
	set_short(&buf[12], keyasc);
	set_short(&buf[14], keycode);

	return pi_write(sd, buf, 16);
}


/***********************************************************************
 *
 * Function:    sys_RPC
 *
 * Summary:     Remote Procedure calls (0x0A, 0x8A)
 *
 * Parameters:  None
 *
 * Returns:     Nothing
 *
 ***********************************************************************/
int
sys_RPC(int sd, int socket, int trap, long *D0, long *A0, int params,
	struct RPC_param *param, int reply)
{
	int 	idx;
	unsigned char buf[4096];
	unsigned char *c;

	buf[0] = socket;	/* 0 for debug, 1 for console */
	buf[1] = socket;
	buf[2] = 0;
	buf[4] = 0x0a;
	buf[5] = 0;

	set_short(buf + 6, trap);
	set_long(buf + 8, *D0);
	set_long(buf + 12, *A0);
	set_short(buf + 16, params);

	c = buf + 18;
	for (idx = params - 1; idx >= 0; idx--) {
		set_byte(c, param[idx].byRef);
		c++;
		set_byte(c, param[idx].size);
		c++;
		if (param[idx].data)
			memcpy(c, param[idx].data, param[idx].size);
		c += param[idx].size;
		if (param[idx].size & 1)
			*c++ = 0;
	}

	if (socket == 3)
		set_short(buf + 4, c - buf - 6);

	pi_write(sd, buf + 4,(size_t)(c - buf - 4));

	if (reply) {
		int l = pi_read(sd, buf, 4096);

		if (l < 0)
			return l;
		if (l < 2)
			return -1;
		if (buf[0] != 0x8a)
			return -2;

		*D0 = get_long(buf + 4);
		*A0 = get_long(buf + 8);
		c = buf + 14;
		for (idx = params - 1; idx >= 0; idx--) {
			if (param[idx].byRef && param[idx].data)
				memcpy(param[idx].data, c + 2,
				       param[idx].size);
			c += 2 + ((get_byte(c + 1) + 1) & ~1);
		}
	}
	return 0;
}


/***********************************************************************
 *
 * Function:    RPC
 *
 * Summary:     Deprecated
 *
 * Parameters:  None
 *
 * Returns:     Nothing
 *
 ***********************************************************************/
int
RPC(int sd, int socket, int trap, int reply, ...)
{
	int 	idx 	= 0,
		j,
		RPC_arg[20];
	va_list ap;
	struct 	RPC_param p[20];
	long 	D0 	= 0,
		A0 	= 0;

	va_start(ap, reply);
	for (;;) {
		int type = va_arg(ap, int);

		if (type == 0)
			break;
		if (type < 0) {
			p[idx].byRef 	= 0;
			p[idx].size 	= -type;
			RPC_arg[idx] 	= va_arg(ap, int);

			p[idx].data 	= &RPC_arg[idx];
			p[idx].invert 	= 0;
		} else {
			void *c = va_arg(ap, void *);

			p[idx].byRef 	= 1;
			p[idx].size 	= type;
			p[idx].data 	= c;
			p[idx].invert 	= va_arg(ap, int);

			if (p[idx].invert) {
				if (p[idx].size == 2) {
					int *s = c;

					*s = htons((short) *s);
				} else {
					int *l = c;

					*l = htonl((unsigned) *l);
				}
			}
		}
		idx++;
	}
	va_end(ap);

	sys_RPCerror =
	    sys_RPC(sd, socket, trap, &D0, &A0, idx, p, reply != 2);

	for (j = 0; j < idx; j++) {
		if (p[j].invert) {
			void *c = p[j].data;

			if (p[j].size == 2) {
				int *s = c;

				*s = htons((short) *s);
			} else {
				int *l = c;

				*l = htonl((unsigned) *l);
			}
		}
	}

	if (reply)
		return A0;
	else
		return D0;
}


/***********************************************************************
 *
 * Function:    PackRPC
 *
 * Summary:     Pack the RPC structure for transmission
 *
 * Parameters:  None
 *
 * Returns:     Nothing
 *
 ***********************************************************************/
int
PackRPC(struct RPC_params *p, int trap, int reply, ...)
{
	int 	idx = 0;
	va_list ap;

	p->trap = trap;
	p->reply = reply;

	va_start(ap, reply);
	for (;;) {
		int type = (int) va_arg(ap, int);

		if (type == 0)
			break;
		if (type < 0) {
			p->param[idx].byRef 	= 0;
			p->param[idx].size 	= -type;
			p->param[idx].arg 	= (int) va_arg(ap, int);

			p->param[idx].data 	= &p->param[idx].arg;
			p->param[idx].invert 	= 0;
		} else {
			void *c = (void *) va_arg(ap, void *);

			p->param[idx].byRef 	= 1;
			p->param[idx].size 	= type;
			p->param[idx].data 	= c;
			p->param[idx].invert 	= (int) va_arg(ap, int);
		}
		idx++;
	}
	p->args = idx;
	va_end(ap);

	return 0;
}


/***********************************************************************
 *
 * Function:    UninvertRPC
 *
 * Summary:     
 *
 * Parameters:  None
 *
 * Returns:     Nothing
 *
 ***********************************************************************/
void
UninvertRPC(struct RPC_params *p)
{
	int 	j;

	for (j = 0; j < p->args; j++) {
		if (p->param[j].invert) {
			void *c = p->param[j].data;

			if ((p->param[j].invert == 2)
			    && (p->param[j].size == 2)) {
				int *s = c;

				*s = htons((short) *s) >> 8;
			} else if (p->param[j].size == 2) {
				int *s = c;

				*s = htons((short) *s);
			} else {
				long *l = c;

				*l = htonl((unsigned) *l);
			}
		}
	}
}


/***********************************************************************
 *
 * Function:    InvertRPC
 *
 * Summary:     
 *
 * Parameters:  None
 *
 * Returns:     Nothing
 *
 ***********************************************************************/
void
InvertRPC(struct RPC_params *p)
{
	int 	j;

	for (j = 0; j < p->args; j++) {
		if (p->param[j].invert) {
			void *c = p->param[j].data;

			if ((p->param[j].invert == 2)
			    && (p->param[j].size == 2)) {
				int *s = c;

				*s = ntohs(*s) >> 8;
			} else if (p->param[j].size == 2) {
				int *s = c;

				*s = ntohs(*s);
			} else {
				long *l = c;

				*l = ntohl((unsigned) *l);
			}
		}
	}
}


/***********************************************************************
 *
 * Function:    DoRPC
 *
 * Summary:     Actually execute the RPC query/response
 *
 * Parameters:  None
 *
 * Returns:     Nothing
 *
 ***********************************************************************/
unsigned long
DoRPC(int sd, int socket, struct RPC_params *p, int *error)
{
	int 	err;
	long 	D0 = 0,
		A0 = 0;

	InvertRPC(p);

	err =
	    sys_RPC(sd, socket, p->trap, &D0, &A0, p->args, &p->param[0],
		    p->reply);

	UninvertRPC(p);

	if (error)
		*error = err;

	if (p->reply == RPC_PtrReply)
		return A0;
	else if (p->reply == RPC_IntReply)
		return D0;
	else
		return err;
}


/***********************************************************************
 *
 * Function:    RPC_Int_Void
 *
 * Summary:     
 *
 * Parameters:  None
 *
 * Returns:     Nothing
 *
 ***********************************************************************/
int
RPC_Int_Void(int sd, int trap)
{
	return RPC(sd, 1, trap, 0, RPC_End);
}


/***********************************************************************
 *
 * Function:    RPC_Ptr_Void
 *
 * Summary:     
 *
 * Parameters:  None
 *
 * Returns:     Nothing
 *
 ***********************************************************************/
int
RPC_Ptr_Void(int sd, int trap)
{
	return RPC(sd, 1, trap, 1, RPC_End);
}


/***********************************************************************
 *
 * Function:    RPC_MemCardInfo
 *
 * Summary:     Untested complex RPC example
 *
 * Parameters:  None
 *
 * Returns:     Nothing
 *
 ***********************************************************************/
int
RPC_MemCardInfo(int sd, int cardno, char * cardname, char * manufname,
                    int * version, long * date, long * romsize, long * ramsize,
                    long * freeram) {
  return RPC(sd, 1, 0xA004, 0, RPC_Short(cardno), RPC_Ptr(cardname, 32), 
                               RPC_Ptr(manufname, 32), RPC_ShortPtr(version),
                               RPC_LongPtr(date), RPC_LongPtr(romsize),
                               RPC_LongPtr(ramsize), RPC_LongPtr(freeram));
}                    

