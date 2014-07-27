/*
 * cortex-a clocks; excludes tegra 2 SoC clocks
 *
 * cortex-a processors include private `global' and local timers
 * at soc.scu + 0x200 (global) and + 0x600 (local).
 * the global timer is a single count-up timer shared by all cores
 * but with per-cpu comparator and auto-increment registers.
 * a local count-down timer can be used as a watchdog.
 *
 * v7 arch provides a 32-bit count-up cycle counter (at about 1GHz in our case)
 * but it's unsuitable as our source of fastticks, because it stops advancing
 * when the cpu is suspended by WFI.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "ureg.h"
#include "arm.h"

enum {
	Debug		= 0,

	Basetickfreq	= 25*Mhz,			/* soc.µs rate in Hz */

	Clockfreqbase	= 25*Mhz,	/* private timer rate */
	Tcycles		= Clockfreqbase / HZ,	/* cycles per clock tick */

	MinPeriod	= Tcycles / 100,
	MaxPeriod	= Tcycles,
};

/* Per-cpu timer on Armada XP */
struct Ltimer {
        ulong ctl;
        ulong _pad0[3];
        ulong load;
        ulong cnt;
        ulong _pad1[(0x68-0x58)/4];
        ulong isr;
};

struct Pglbtmr {
        ulong ctl;
        ulong status;
        ulong cnt[2]; 
};


typedef struct Ltimer Ltimer;
typedef struct Pglbtmr Pglbtmr;

enum {
	/* ctl bits */
	Tmr0ena	= 1<<0,		/* timer 0 enabled */
	Tmr0auto = 1<<1,        /* reload on intr; periodic interrupts */
        Tmr0ref25mhz = 1<<11,
        
	/* isr bits */
	Xisrclk	= ~(1<<0),      /* RW0C */

};


enum {
	Gtmrena	= 1<<0,
};

/*
 * until 5[cl] inline vlong ops, avoid them where possible,
 * they are currently slow function calls.
 */
typedef union Vlong Vlong;
union Vlong {
	uvlong	uvl;
	struct {			/* little-endian */
		ulong	low;
		ulong	high;
	};
};


/* no lock is needed to update our local timer.  splhi keeps it tight. */
static void
setltimer(Ltimer *tn, ulong ticks)
{
	int s;

	assert(ticks <= Clockfreqbase);
	s = splhi();
	tn->load = tn->cnt = ticks - 1;
	coherence();
	tn->ctl = Tmr0ena | Tmr0ref25mhz | Tmr0auto;
	coherence();
	splx(s);
}

void serialkick(void);

static void
clockintr(Ureg* ureg, void *arg)
{
	Ltimer *tn;

	tn = (Ltimer*)arg;
	tn->isr = Xisrclk;

	coherence();
	/* iprint("."); */
	serialkick();
	timerintr(ureg, 0);
}



/*
 * the local timer is the interrupting timer and does not
 * participate in measuring time.  It is initially set to HZ.
 */
void
clockinit(void)
{
	ulong old;
	Ltimer *tn;
        Pglbtmr* gt = (Pglbtmr*)soc.glbtmr;

	/* turn my cycle counter on */
	cpwrsc(0, CpCLD, CpCLDena, CpCLDenacyc, 1<<31);

	/* turn all my counters on and clear my cycle counter */
	cpwrsc(0, CpCLD, CpCLDena, CpCLDenapmnc, 1<<2 | 1);

	/* let users read my cycle counter directly */
	cpwrsc(0, CpCLD, CpCLDuser, CpCLDenapmnc, 1);

        gt->ctl = Gtmrena;

	tn = (Ltimer *)soc.loctmr;
	if (m->machno == 0)
		irqenable(Loctmrirq, clockintr, tn, "clock");
	else
		intcunmask(Loctmrirq);

        tn->load = tn->cnt = Clockfreqbase / 1000;
	tn->isr = Xisrclk;
	coherence();
	tn->ctl = Tmr0ena | Tmr0ref25mhz;
	coherence();

	old = tn->cnt;
	delay(5);

	if (tn->cnt == old)
		panic("cpu%d: clock not ticking at all", m->machno);

	delay(m->machno*2);
	setltimer(tn, Tcycles);
}


static void
wallclock(uvlong *x)
{
        Pglbtmr* gt = (Pglbtmr*)soc.glbtmr;
        
	ulong hi, newhi, lo, *y;
	newhi = gt->cnt[1];
	do{
		hi = newhi;
		lo = gt->cnt[0];
	}while((newhi = gt->cnt[1]) != hi);
	y = (ulong *) x;
	y[0] = lo;
	y[1] = hi;
}

long lcycles()
{
        uvlong now;
        wallclock(&now);
        return (long)now;
}



/* our fastticks are at 1MHz (Basetickfreq), so the conversion is trivial. */
ulong
µs(void)
{
	return fastticks2us(fastticks(nil));
}

uvlong
fastticks(uvlong *hz)
{
	uvlong ret;

	if(hz != nil)
		*hz = Basetickfreq;
	wallclock(&ret);
	return ret;
}

ulong perfticks(void)
{
        uvlong ret;
        wallclock(&ret);
        return (ulong)ret;
}



static void
waituntil(uvlong n)
{
	uvlong now, then;
	
	wallclock(&now);
	then = now + n;
	while(now < then)
		wallclock(&now);
}

void
microdelay(int n)
{
	waituntil(((uvlong)n) * Basetickfreq / 1000000);
}

void
delay(int n)
{
	waituntil(((uvlong)n) * Basetickfreq / 1000);
}


/* Tval is supposed to be in fastticks units. */
void
timerset(Tval next)
{
	int s;
	long offset;
	Ltimer *tn;

	tn = (Ltimer *)soc.loctmr;
	s = splhi();
	offset = fastticks2us(next - fastticks(nil));
	/* offset is now in µs (MHz); convert to Clockfreqbase Hz. */
	offset *= Clockfreqbase / Mhz;
	if(offset < MinPeriod)
		offset = MinPeriod;
	else if(offset > MaxPeriod)
		offset = MaxPeriod;

	setltimer(tn, offset);
	splx(s);
}

void clockshutdown(void) {}

