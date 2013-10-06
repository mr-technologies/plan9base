#include "u.h"
#include "ureg.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

static ulong globalcycleshi,globalcycleslo;

extern uchar *periph;
ulong *local;
enum {
	PERIPHCLK = 200000000,
	MaxPeriod = PERIPHCLK / (256 * 100),
	MinPeriod = MaxPeriod / 100,
} ;

void
globalclockinit(void)
{
	local = (ulong*) (periph + 0x600);
	local[8]=0xFFFFFFFF;
	local[9]=0xFFFFFFFF;
	local[10]=0x0003;
}


void
cycles(uvlong *x)
{
	ulong lo;
	lo = ~local[9];
	if (lo < globalcycleslo) {
		++globalcycleshi;
	}
	globalcycleslo=lo;
	*x = (uvlong)lo|(((uvlong)globalcycleshi)<<32);
}

uvlong
fastticks(uvlong *hz)
{
	uvlong ret;

	if(hz != nil)
		*hz = PERIPHCLK;
	cycles(&ret);
	return ret;
}

ulong
µs(void)
{
	return fastticks2us(fastticks(nil));
}


ulong
perfticks(void)
{
	return globalcycleslo;
}

void
clocktick(Ureg* ureg, void *)
{
	local[3]=1;
	timerintr(ureg, 0);
}

void
localclockinit(void)
{
	local[2] = 0xFF06;
	intenable(29, clocktick, nil);
	timerset(0);
}

void
timerset(uvlong val)
{
	uvlong now, ticks;

	if(val == 0)
		ticks = MaxPeriod;
	else{
		cycles(&now);
		ticks = (val - now) / 256;
		if(ticks < MinPeriod)
			ticks = MinPeriod;
		if(ticks > MaxPeriod)
			ticks = MaxPeriod;
	}
	local[2] &= ~1;
	local[0] = local[1] = ticks;
	local[2] |= 1;
}

static void
waituntil(uvlong n)
{
	uvlong now, then;
	
	cycles(&now);
	then = now + n;
	while(now < then)
		cycles(&now);
}

void
microdelay(int n)
{
	waituntil(((uvlong)n) * PERIPHCLK / 1000000);
}

void
delay(int n)
{
	waituntil(((uvlong)n) * PERIPHCLK / 1000);
}

