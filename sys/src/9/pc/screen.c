#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"
#include "../port/error.h"

#define	Image	IMAGE
#include <draw.h>
#include <memdraw.h>
#include <cursor.h>
#include "screen.h"

#define RGB2K(r,g,b)	((156763*(r)+307758*(g)+59769*(b))>>19)

Point ZP = {0, 0};

Rectangle physgscreenr;

Memimage *gscreen;

VGAscr vgascreen[1];

Cursor arrow = {
	{ -1, -1 },
	{ 0xFF, 0xFF, 0x80, 0x01, 0x80, 0x02, 0x80, 0x0C, 
	  0x80, 0x10, 0x80, 0x10, 0x80, 0x08, 0x80, 0x04, 
	  0x80, 0x02, 0x80, 0x01, 0x80, 0x02, 0x8C, 0x04, 
	  0x92, 0x08, 0x91, 0x10, 0xA0, 0xA0, 0xC0, 0x40, 
	},
	{ 0x00, 0x00, 0x7F, 0xFE, 0x7F, 0xFC, 0x7F, 0xF0, 
	  0x7F, 0xE0, 0x7F, 0xE0, 0x7F, 0xF0, 0x7F, 0xF8, 
	  0x7F, 0xFC, 0x7F, 0xFE, 0x7F, 0xFC, 0x73, 0xF8, 
	  0x61, 0xF0, 0x60, 0xE0, 0x40, 0x40, 0x00, 0x00, 
	},
};

int
screensize(int x, int y, int, ulong chan)
{
	VGAscr *scr;

	qlock(&drawlock);
	if(waserror()){
		qunlock(&drawlock);
		nexterror();
	}

	memimageinit();

	lock(&vgascreenlock);
	if(waserror()){
		unlock(&vgascreenlock);
		nexterror();
	}

	scr = &vgascreen[0];
	scr->gscreendata = nil;
	scr->gscreen = nil;
	if(gscreen){
		freememimage(gscreen);
		gscreen = nil;
	}

	if(scr->paddr == 0){
		if(scr->dev && scr->dev->page){
			scr->vaddr = KADDR(VGAMEM());
			scr->apsize = 1<<16;
		}
		scr->softscreen = 1;
	}
	if(scr->softscreen){
		gscreen = allocmemimage(Rect(0,0,x,y), chan);
		scr->useflush = 1;
	}else{
		static Memdata md;

		md.ref = 1;
		if((md.bdata = scr->vaddr) == 0)
			error("framebuffer not maped");
		gscreen = allocmemimaged(Rect(0,0,x,y), chan, &md);
		scr->useflush = scr->dev && scr->dev->flush;
	}
	if(gscreen == nil)
		error("no memory for vga memimage");

	scr->palettedepth = 6;	/* default */
	scr->memdefont = getmemdefont();
	scr->gscreen = gscreen;
	scr->gscreendata = gscreen->data;

	physgscreenr = gscreen->r;

	vgaimageinit(chan);

	unlock(&vgascreenlock);
	poperror();

	drawcmap();
	swcursorinit();

	qunlock(&drawlock);
	poperror();

	return 0;
}

int
screenaperture(int size, int align)
{
	VGAscr *scr;

	scr = &vgascreen[0];

	if(scr->paddr)	/* set up during enable */
		return 0;

	if(size == 0)
		return 0;

	if(scr->dev && scr->dev->linear){
		scr->dev->linear(scr, size, align);
		return 0;
	}

	/*
	 * Need to allocate some physical address space.
	 * The driver will tell the card to use it.
	 */
	size = PGROUND(size);
	scr->paddr = upaalloc(size, align);
	if(scr->paddr == 0)
		return -1;
	scr->vaddr = vmap(scr->paddr, size);
	if(scr->vaddr == nil)
		return -1;
	scr->apsize = size;

	return 0;
}

uchar*
attachscreen(Rectangle* r, ulong* chan, int* d, int* width, int *softscreen)
{
	VGAscr *scr;

	scr = &vgascreen[0];
	if(scr->gscreen == nil || scr->gscreendata == nil)
		return nil;

	*r = scr->gscreen->clipr;
	*chan = scr->gscreen->chan;
	*d = scr->gscreen->depth;
	*width = scr->gscreen->width;
	if(scr->gscreendata->allocd){
		/*
		 * we use a memimage as softscreen. devdraw will create its own
		 * screen image on the backing store of that image. when our gscreen
		 * and devdraws screenimage gets freed, the imagedata will
		 * be released.
		 */
		*softscreen = 0xa110c;
		scr->gscreendata->ref++;
	} else
		*softscreen = scr->useflush ? 1 : 0;
	return scr->gscreendata->bdata;
}

void
flushmemscreen(Rectangle r)
{
	VGAscr *scr;
	uchar *sp, *disp, *sdisp, *edisp;
	int y, len, incs, off, page;

	scr = &vgascreen[0];
	if(scr->gscreen == nil || scr->useflush == 0)
		return;
	if(scr->dev && scr->dev->flush){
		scr->dev->flush(scr, r);
		return;
	}
	if(rectclip(&r, scr->gscreen->r) == 0)
		return;
	disp = scr->vaddr;
	incs = scr->gscreen->width*BY2WD;
	off = (r.min.x*scr->gscreen->depth) / 8;
	len = (r.max.x*scr->gscreen->depth + 7) / 8;
	len -= off;
	off += r.min.y*incs;
	sp = scr->gscreendata->bdata + scr->gscreen->zero + off;

	/*
	 * Linear framebuffer with softscreen.
	 */
	if(scr->paddr){
		sdisp = disp+off;
		for(y = r.min.y; y < r.max.y; y++) {
			memmove(sdisp, sp, len);
			sp += incs;
			sdisp += incs;
		}
		return;
	}

	/*
	 * Paged framebuffer window.
	 */
	if(scr->dev == nil || scr->dev->page == nil)
		return;

	page = off/scr->apsize;
	off %= scr->apsize;
	sdisp = disp+off;
	edisp = disp+scr->apsize;

	scr->dev->page(scr, page);
	for(y = r.min.y; y < r.max.y; y++) {
		if(sdisp + incs < edisp) {
			memmove(sdisp, sp, len);
			sp += incs;
			sdisp += incs;
		}
		else {
			off = edisp - sdisp;
			page++;
			if(off <= len){
				if(off > 0)
					memmove(sdisp, sp, off);
				scr->dev->page(scr, page);
				if(len - off > 0)
					memmove(disp, sp+off, len - off);
			}
			else {
				memmove(sdisp, sp, len);
				scr->dev->page(scr, page);
			}
			sp += incs;
			sdisp += incs - scr->apsize;
		}
	}
}

void
getcolor(ulong p, ulong* pr, ulong* pg, ulong* pb)
{
	VGAscr *scr;
	ulong x;

	scr = &vgascreen[0];
	if(scr->gscreen == nil)
		return;

	switch(scr->gscreen->depth){
	default:
		x = 0x0F;
		break;
	case 8:
		x = 0xFF;
		break;
	}
	p &= x;

	lock(&cursor);
	*pr = scr->colormap[p][0];
	*pg = scr->colormap[p][1];
	*pb = scr->colormap[p][2];
	unlock(&cursor);
}

int
setpalette(ulong p, ulong r, ulong g, ulong b)
{
	VGAscr *scr;
	int d;

	scr = &vgascreen[0];
	d = scr->palettedepth;

	lock(&cursor);
	scr->colormap[p][0] = r;
	scr->colormap[p][1] = g;
	scr->colormap[p][2] = b;
	vgao(PaddrW, p);
	vgao(Pdata, r>>(32-d));
	vgao(Pdata, g>>(32-d));
	vgao(Pdata, b>>(32-d));
	unlock(&cursor);

	return ~0;
}

/*
 * On some video cards (e.g. Mach64), the palette is used as the 
 * DAC registers for >8-bit modes.  We don't want to set them when the user
 * is trying to set a colormap and the card is in one of these modes.
 */
int
setcolor(ulong p, ulong r, ulong g, ulong b)
{
	VGAscr *scr;
	int x;

	scr = &vgascreen[0];
	if(scr->gscreen == nil)
		return 0;

	switch(scr->gscreen->depth){
	case 1:
	case 2:
	case 4:
		x = 0x0F;
		break;
	case 8:
		x = 0xFF;
		break;
	default:
		return 0;
	}
	p &= x;

	return setpalette(p, r, g, b);
}

void
swenable(VGAscr*)
{
	swcursorload(&arrow);
}

void
swdisable(VGAscr*)
{
}

void
swload(VGAscr*, Cursor *curs)
{
	swcursorload(curs);
}

int
swmove(VGAscr*, Point p)
{
	swcursorhide();
	swcursordraw(p);
	return 0;
}

VGAcur swcursor =
{
	"soft",
	swenable,
	swdisable,
	swload,
	swmove,
};


void
cursoron(void)
{
	VGAscr *scr;
	VGAcur *cur;

	scr = &vgascreen[0];
	cur = scr->cur;
	if(cur == nil || cur->move == nil)
		return;

	if(cur == &swcursor)
		qlock(&drawlock);
	lock(&cursor);
	cur->move(scr, mousexy());
	unlock(&cursor);
	if(cur == &swcursor)
		qunlock(&drawlock);
}

void
cursoroff(void)
{
}

void
setcursor(Cursor* curs)
{
	VGAscr *scr;

	scr = &vgascreen[0];
	if(scr->cur == nil || scr->cur->load == nil)
		return;

	scr->cur->load(scr, curs);
}

int hwaccel = 1;
int hwblank = 0;	/* turned on by drivers that are known good */
int panning = 0;

int
hwdraw(Memdrawparam *par)
{
	VGAscr *scr;
	Memimage *dst, *src, *mask;
	Memdata *scrd;
	int m;

	scr = &vgascreen[0];
	scrd = scr->gscreendata;
	if(scr->gscreen == nil || scrd == nil)
		return 0;
	if((dst = par->dst) == nil || dst->data == nil)
		return 0;
	if((src = par->src) && src->data == nil)
		src = nil;
	if((mask = par->mask) && mask->data == nil)
		mask = nil;
	if(scr->cur == &swcursor){
		if(dst->data->bdata == scrd->bdata)
			swcursoravoid(par->r);
		if(src && src->data->bdata == scrd->bdata)
			swcursoravoid(par->sr);
		if(mask && mask->data->bdata == scrd->bdata)
			swcursoravoid(par->mr);
	}
	if(hwaccel == 0)
		return 0;
	if(dst->data->bdata != scrd->bdata || src == nil || mask == nil)
		return 0;
	if(scr->fill==nil && scr->scroll==nil)
		return 0;

	/*
	 * If we have an opaque mask and source is one opaque
	 * pixel we can convert to the destination format and just
	 * replicate with memset.
	 */
	m = Simplesrc|Simplemask|Fullmask;
	if(scr->fill
	&& (par->state&m)==m
	&& ((par->srgba&0xFF) == 0xFF)
	&& (par->op&S) == S)
		return scr->fill(scr, par->r, par->sdval);

	/*
	 * If no source alpha, an opaque mask, we can just copy the
	 * source onto the destination.  If the channels are the same and
	 * the source is not replicated, memmove suffices.
	 */
	m = Simplemask|Fullmask;
	if(scr->scroll
	&& src->data->bdata==dst->data->bdata
	&& !(src->flags&Falpha)
	&& (par->state&m)==m
	&& (par->op&S) == S)
		return scr->scroll(scr, par->r, par->sr);

	return 0;	
}

void
blankscreen(int blank)
{
	VGAscr *scr;

	scr = &vgascreen[0];
	if(hwblank){
		if(scr->blank)
			scr->blank(scr, blank);
		else
			vgablank(scr, blank);
	}
}

void
vgalinearpci(VGAscr *scr)
{
	ulong paddr;
	int i, size, best;
	Pcidev *p;
	
	p = scr->pci;
	if(p == nil)
		return;

	/*
	 * Scan for largest memory region on card.
	 * Some S3 cards (e.g. Savage) have enormous
	 * mmio regions (but even larger frame buffers).
	 * Some 3dfx cards (e.g., Voodoo3) have mmio
	 * buffers the same size as the frame buffer,
	 * but only the frame buffer is marked as
	 * prefetchable (bar&8).  If a card doesn't fit
	 * into these heuristics, its driver will have to
	 * call vgalinearaddr directly.
	 */
	best = -1;
	for(i=0; i<nelem(p->mem); i++){
		if(p->mem[i].bar&1)	/* not memory */
			continue;
		if(p->mem[i].size < 640*480)	/* not big enough */
			continue;
		if(best==-1 
		|| p->mem[i].size > p->mem[best].size 
		|| (p->mem[i].size == p->mem[best].size 
		  && (p->mem[i].bar&8)
		  && !(p->mem[best].bar&8)))
			best = i;
	}
	if(best >= 0){
		paddr = p->mem[best].bar & ~0x0F;
		size = p->mem[best].size;
		vgalinearaddr(scr, paddr, size);
		return;
	}
	error("no video memory found on pci card");
}

void
vgalinearaddr(VGAscr *scr, ulong paddr, int size)
{
	int x, nsize;
	ulong npaddr;

	/*
	 * new approach.  instead of trying to resize this
	 * later, let's assume that we can just allocate the
	 * entire window to start with.
	 */

	if(scr->paddr == paddr && size <= scr->apsize)
		return;

	if(scr->paddr){
		/*
		 * could call vunmap and vmap,
		 * but worried about dangling pointers in devdraw
		 */
		error("cannot grow vga frame buffer");
	}
	
	/* round to page boundary, just in case */
	x = paddr&(BY2PG-1);
	npaddr = paddr-x;
	nsize = PGROUND(size+x);

	/*
	 * Don't bother trying to map more than 4000x4000x32 = 64MB.
	 * We only have a 256MB window.
	 */
	if(nsize > 64*MB)
		nsize = 64*MB;
	scr->vaddr = vmap(npaddr, nsize);
	if(scr->vaddr == 0)
		error("cannot allocate vga frame buffer");
	scr->vaddr = (char*)scr->vaddr+x;
	scr->paddr = paddr;
	scr->apsize = nsize;
	/* let mtrr harmlessly fail on old CPUs, e.g., P54C */
	if(!waserror()){
		mtrr(npaddr, nsize, "wc");
		poperror();
	}
}
