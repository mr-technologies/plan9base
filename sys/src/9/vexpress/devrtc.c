/*
 * devrtc - real-time clock, for kirkwood
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "io.h"

typedef	struct	RtcReg	RtcReg;

struct RtcReg
{
	ulong	dr;
	ulong	mr;
	ulong	lr;
	ulong	cr;
	ulong	intrmask;
	ulong	mis;
	ulong	ris;
	ulong	icr;
};

struct Rtc
{
	int	sec;
	int	min;
	int	hour;
	int	wday;
	int	mday;
	int	mon;
	int	year;
};

enum {
	Qdir,
	Qrtc,
};

static Dirtab rtcdir[] = {
	".",	{Qdir, 0, QTDIR},	0,		0555,
	"rtc",	{Qrtc},			NUMSIZE,	0664,
};
static	RtcReg	*rtcreg;		/* filled in by attach */
static	Lock	rtclock;

#define SEC2MIN	60
#define SEC2HOUR (60*SEC2MIN)
#define SEC2DAY (24L*SEC2HOUR)

/*
 * days per month plus days/year
 */
static	int	dmsize[] =
{
	365, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};
static	int	ldmsize[] =
{
	366, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/*
 *  return the days/month for the given year
 */
static int *
yrsize(int yr)
{
	if((yr % 4) == 0)
		return ldmsize;
	else
		return dmsize;
}

enum {
	Rtcsec	= 0x00007f,
	Rtcmin	= 0x007f00,
	Rtcms	= 8,
	Rtchr12	= 0x1f0000,
	Rtchr24	= 0x3f0000,
	Rtchrs	= 16,

	Rdmday	= 0x00003f,
	Rdmon	= 0x001f00,
	Rdms	= 8,
	Rdyear	= 0x7f0000,
	Rdys	= 16,

	Rtcpm	= 1<<21,		/* pm bit */
	Rtc12	= 1<<22,		/* 12 hr clock */
};

static ulong
bcd2dec(ulong bcd)
{
	ulong d, m, i;

	d = 0;
	m = 1;
	for(i = 0; i < 2 * sizeof d; i++){
		d += ((bcd >> (4*i)) & 0xf) * m;
		m *= 10;
	}
	return d;
}

static ulong
dec2bcd(ulong d)
{
	ulong bcd, i;

	bcd = 0;
	for(i = 0; d != 0; i++){
		bcd |= (d%10) << (4*i);
		d /= 10;
	}
	return bcd;
}

long
rtctime(void)
{
	return (long)rtcreg->dr;
}

static Chan*
rtcattach(char *spec)
{
	if (!rtcreg)
		rtcreg = (RtcReg*)vmap(0x10017000,BY2PG);
	return devattach(L'r', spec);
}

static Walkqid*
rtcwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, rtcdir, nelem(rtcdir), devgen);
}

static int
rtcstat(Chan *c, uchar *dp, int n)
{
	return devstat(c, dp, n, rtcdir, nelem(rtcdir), devgen);
}

static Chan*
rtcopen(Chan *c, int omode)
{
	return devopen(c, omode, rtcdir, nelem(rtcdir), devgen);
}

static void
rtcclose(Chan*)
{
}

static long
rtcread(Chan *c, void *buf, long n, vlong off)
{
	if(c->qid.type & QTDIR)
		return devdirread(c, buf, n, rtcdir, nelem(rtcdir), devgen);

	switch((ulong)c->qid.path){
	default:
		error(Egreg);
	case Qrtc:
		return readnum(off, buf, n, rtctime(), NUMSIZE);
	}
}

static long
rtcwrite(Chan *c, void *buf, long n, vlong off)
{
	ulong offset = off;
	char *cp, sbuf[32];

	switch((ulong)c->qid.path){
	default:
		error(Egreg);
	case Qrtc:
		if(offset != 0 || n >= sizeof(sbuf)-1)
			error(Ebadarg);
		memmove(sbuf, buf, n);
		sbuf[n] = '\0';
		for(cp = sbuf; *cp != '\0'; cp++)
			if(*cp >= '0' && *cp <= '9')
				break;
		rtcreg->lr = strtoul(cp, 0, 0);
		return n;
	}
}

Dev rtcdevtab = {
	L'r',
	"rtc",

	devreset,
	devinit,
	devshutdown,
	rtcattach,
	rtcwalk,
	rtcstat,
	rtcopen,
	devcreate,
	rtcclose,
	rtcread,
	devbread,
	rtcwrite,
	devbwrite,
	devremove,
	devwstat,
	devpower,
};
