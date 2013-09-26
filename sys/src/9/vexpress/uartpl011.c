#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

enum {                          /* registers */
  Dr	= 0x000/4,
  Rsr	= 0x000/4,
  Ecr	= 0x004/4,
  Fr	= 0x018/4,
  Ilpr	= 0x020/4,
  Ibrd	= 0x024/4,
  Fbrd	= 0x028/4,
  Lcr_H	= 0x02C/4,
  Cr	= 0x030/4,
  Ifls	= 0x034/4,
  Imsc	= 0x038/4,
  Ris	= 0x03C/4,
  Mis	= 0x040/4,
  Icr	= 0x044/4,
  Dmacr	= 0x048/4,
};

enum {                          /* Dr (read) */
  OE = (1<<11),
  BE = (1<<10),
  PE = (1<<9),
  FE = (1<<8),
};

enum {                          /* Cr */
  CtsEn	= (1<<15),
  RtsEn	= (1<<14),
  Out2	= (1<<13),
  Out1	= (1<<12),
  RtsC	= (1<<11),
  DtrC	= (1<<10),
  Rxe	= (1<<9),
  Txe	= (1<<8),
  Lbe	= (1<<7),
  Ena	= (1<<0),
};

enum {                          /* Fr */
  Ring	= (1<<8),
  Txfe	= (1<<7),
  Rxff	= (1<<6),
  Txff	= (1<<5),
  Rxfe	= (1<<4),
  Busy	= (1<<3),
  DcdC	= (1<<2),
  DsrC	= (1<<1),
  CtsC	= (1<<0),
};

enum {                          /* Lcr_H */
  Sps = (1<<7),
  Fen = (1<<4),
  Stp2 = (1<<3),
  Eps = (1<<2),
  Pen = (1<<1),
  Brk = (1<<0),
};
  
extern ulong *uart;
extern PhysUart pl011physuart;
static Uart puart = {
	.phys = &pl011physuart,
	.bits = 8,
	.stop = 1,
	.baud = 38400,
	.parity = 'n',
};

static Uart *
pl011pnp(void)
{
	return &puart;
}

static void
pl011kick(Uart *u)
{
	int x;
	
	x = splhi();
        while((uart[Fr] & Txff)==0) {
          if (u->op >= u->oe) {
            if (uartstageoutput(u)==0)
              break;
          }
          uart[Dr]=*u->op++;
        }
	splx(x);
}

void
pl011interrupt(Ureg *, void *)
{
	ulong st;

	st = uart[2];
	if((st & 1) != 0)
		return;
	switch((st >> 1) & 0x1F){
	case 0:
	case 16:
		puart.cts = (uart[6] & (1<<4)) != 0;
		puart.dsr = (uart[6] & (1<<5)) != 0;
		puart.dcd = (uart[6] & (1<<7)) != 0;
		break;
	case 1:
		uartkick(&puart);
		break;
	case 2:
	case 6:
		while(uart[5] & 1)
			uartrecv(&puart, uart[0]);
		break;
	default:
		print("unknown UART interrupt %uld\n", (st>>1) & 0x1F);
		uartkick(&puart);
	}
}

static void
pl011enable(Uart *u, int ie)
{
	while(uart[Fr] & Busy)
          ;
        uart[Cr]=CtsEn|RtsEn|Rxe|Txe|Ena;
}

static void
pl011disable(Uart *)
{
	uart[Cr] = 0;
}

static void
pl011dobreak(Uart *, int ms)
{
	if(ms <= 0)
		ms = 200;
	
	uart[Lcr_H] |= Brk;
	tsleep(&up->sleep, return0, 0, ms);
	uart[Lcr_H] &= ~Brk;
}

static int
pl011baud(Uart *u, int baud)
{
	int val;

	if(baud <= 0)
		return -1;

	val = 460800 / baud;
	uart[Ibrd] = val;
	uart[Fbrd] = 0;
        uart[Lcr_H] = Fen|0x60;
	u->baud = baud;
	return 0;
}

static int
pl011bits(Uart *u, int bits)
{
	if(bits < 5 || bits > 8)
		return -1;
	
        uart[Lcr_H] = (uart[Lcr_H] & ~0x60)|((bits-5)*0x20);
	u->bits = bits;
	return 0;
}

static int
pl011stop(Uart *u, int stop)
{
	if(stop < 1 || stop > 2)
		return -1;
	
        uart[Lcr_H] = (uart[Lcr_H] & ~Stp2)|(stop==2?Stp2:0);
	u->stop = stop;
	return 0;
}

static int
pl011parity(Uart *u, int parity)
{
        uart[Lcr_H] &= ~(Pen|Eps);
	switch(parity){
	case 'n':
		break;
	case 'o':
                uart[Lcr_H] |= Pen;
                break;
	case 'e':
                uart[Lcr_H] |= Pen|Eps;
		break;
	default:
		return -1;
	}
	u->parity = parity;
	return 0;
}

static void
pl011modemctl(Uart *u, int on)
{
  /* if(on){ */
    
  /*       	u->modem = 1; */
  /*       	u->cts = (uart[6] & (1<<4)) != 0; */
  /*       	uart[1] |= (1<<6); */
  /*       }else{ */
  /*       	u->modem = 0; */
  /*       	u->cts = 1; */
  /*       } */
}

static void
pl011rts(Uart *, int i)
{
	/* uart[4] = (uart[4] & ~2) | (i << 1); */
}

static void
pl011dtr(Uart *, int i)
{
	/* uart[4] = (uart[4] & ~1) | i; */
}

static long
pl011status(Uart* u, void* buf, long n, long offset)
{
	char *p;
	ulong msr;

	msr = uart[Fr];
	p = malloc(READSTR);
	snprint(p, READSTR,
		"b%d c%d d%d e%d l%d m%d p%c r%d s%d\n"
		"dev(%d) type(%d) framing(%d) overruns(%d) "
		"berr(%d) serr(%d)%s%s%s%s\n",

		u->baud,
		u->hup_dcd, 
		u->dsr,
		u->hup_dsr,
		u->bits,
		u->modem,
		u->parity,
		1, /* (uart[3] & 2) != 0, */
		u->stop,

		u->dev,
		u->type,
		u->ferr,
		u->oerr,
		u->berr,
		u->serr,
		(!(msr & CtsC)) ? " cts": "",
		(!(msr & DsrC)) ? " dsr": "",
		(!(msr & DcdC)) ? " dcd": "",
		(!(msr & Ring)) ? " ri": ""
	);
	n = readstr(offset, buf, n, p);
	free(p);

	return n;
}

static int
pl011getc(Uart *)
{
	while((uart[Fr] & Rxfe)!=0)
		;
	return uart[Dr];
}

static void
pl011putc(Uart *, int c)
{
	while(uart[Fr] & Txff)
		;
	uart[Dr] = c;
}

PhysUart pl011physuart = {
	.name = "pl0114430 uart",
	.pnp = pl011pnp,
	.getc = pl011getc,
	.putc = pl011putc,
	.enable = pl011enable,
	.disable = pl011disable,
	.kick = pl011kick,
	.rts = pl011rts,
	.parity = pl011parity,
	.baud = pl011baud,
	.bits = pl011bits,
	.stop = pl011stop,
	.modemctl = pl011modemctl,
	.dtr = pl011dtr,
	.status = pl011status,
};

void
uartinit(void)
{
	consuart = &puart;
	puart.console = 1;
}

Uart* uartenable(Uart*);

int
i8250console(void)
{
	uartinit();
	uartenable(consuart);
	consuart->opens++;
	uartctl(consuart,"b38400 18 pn r1 s1 i1");
	return 0;
}
