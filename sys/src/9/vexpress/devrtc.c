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

enum {
	Qdir,
	Qrtc,
};

static Dirtab rtcdir[] = {
	".",	{Qdir, 0, QTDIR},	0,		0555,
	"rtc",	{Qrtc},			NUMSIZE,	0664,
};
static	RtcReg	*rtcreg;		/* filled in by attach */

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
