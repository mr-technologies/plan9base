#include <u.h>
#include <libc.h>
#include <bio.h>
#include <auth.h>
#include <fcall.h>
#include <disk.h>

enum {
	LEN	= 8*1024,
	HUNKS	= 128,
};

typedef struct File File;
struct File{
	char	*new;
	char	*elem;
	char	*old;
	char	*uid;
	char	*gid;
	ulong	mode;
};

typedef void Mkfserr(char*, void*);
typedef void Mkfsenum(char*, char*, Dir*, void*);

typedef struct Name Name;
struct Name {
	int n;
	char *s;
};

typedef struct Opt Opt;
struct Opt {
	int level;
	char *skip;
	char *uid;
	char *gid;
	Opt *prev;
};

typedef struct Mkaux Mkaux;
struct Mkaux {
	Mkfserr *warn;
	Mkfsenum *mkenum;
	char *root;
	char *proto;
	jmp_buf jmp;
	Biobuf *b;

	Name oldfile;
	Name fullname;
	int	lineno;
	int	indent;

	Opt *opt;

	void *a;
};

static void domkfs(Mkaux *mkaux, File *me, int level);

static int	copyfile(Mkaux*, File*, Dir*, int);
static void	freefile(File*);
static File*	getfile(Mkaux*, File*);
static char*	getmode(Mkaux*, char*, ulong*);
static char*	getname(Mkaux*, char*, char**);
static char*	getpath(Mkaux*, char*);
static int	mkfile(Mkaux*, File*);
static char*	mkpath(Mkaux*, char*, char*);
static void	mktree(Mkaux*, File*, int);
static void	setname(Mkaux*, Name*, File*);
static void	skipdir(Mkaux*);
static void	warn(Mkaux*, char *, ...);
static void	popopt(Mkaux *mkaux);

//static void
//mprint(char *new, char *old, Dir *d, void*)
//{
//	print("%s %s %D\n", new, old, d);
//}

int
rdproto(char *proto, char *root, Mkfsenum *mkenum, Mkfserr *mkerr, void *a)
{
	Mkaux mx, *m;
	File file;
	int rv;

	m = &mx;
	memset(&mx, 0, sizeof mx);
	if(root == nil)
		root = "/";

	m->root = root;
	m->warn = mkerr;
	m->mkenum = mkenum;
	m->a = a;
	m->proto = proto;
	m->lineno = 0;
	m->indent = 0;
	m->opt = nil;
	if((m->b = Bopen(proto, OREAD)) == nil) {
		werrstr("open '%s': %r", proto);
		return -1;
	}

	memset(&file, 0, sizeof file);
	file.new = "";
	file.old = nil;

	rv = 0;
	if(setjmp(m->jmp) == 0)
		domkfs(m, &file, -1);
	else
		rv = -1;
	free(m->oldfile.s);
	free(m->fullname.s);
	m->indent = -1;
	popopt(m);
	return rv;
}

static void*
emalloc(Mkaux *mkaux, ulong n)
{
	void *v;

	v = malloc(n);
	if(v == nil)
		longjmp(mkaux->jmp, 1);	/* memory leak */
	memset(v, 0, n);
	return v;
}

static char*
estrdup(Mkaux *mkaux, char *s)
{
	s = strdup(s);
	if(s == nil)
		longjmp(mkaux->jmp, 1);	/* memory leak */
	return s;
}

static void
domkfs(Mkaux *mkaux, File *me, int level)
{
	File *child;
	int rec;

	child = getfile(mkaux, me);
	if(!child)
		return;
	if((child->elem[0] == '+' || child->elem[0] == '*') && child->elem[1] == '\0'){
		rec = child->elem[0] == '+';
		free(child->new);
		child->new = estrdup(mkaux, me->new);
		setname(mkaux, &mkaux->oldfile, child);
		mktree(mkaux, child, rec);
		freefile(child);
		child = getfile(mkaux, me);
	}
	while(child && mkaux->indent > level){
		if(mkfile(mkaux, child))
			domkfs(mkaux, child, mkaux->indent);
		freefile(child);
		child = getfile(mkaux, me);
	}
	if(child){
		freefile(child);
		Bseek(mkaux->b, -Blinelen(mkaux->b), 1);
		mkaux->lineno--;
	}
}

static void
mktree(Mkaux *mkaux, File *me, int rec)
{
	File child;
	Dir *d;
	int i, n, fd;

	fd = open(mkaux->oldfile.s, OREAD);
	if(fd < 0){
		warn(mkaux, "can't open %s: %r", mkaux->oldfile.s);
		return;
	}
	child = *me;
	while((n = dirread(fd, &d)) > 0){
		for(i = 0; i < n; i++){
			if(mkaux->opt && mkaux->opt->skip)
				if(strstr(d[i].name, mkaux->opt->skip))
					continue;
			child.new = mkpath(mkaux, me->new, d[i].name);
			if(me->old)
				child.old = mkpath(mkaux, me->old, d[i].name);
			child.elem = d[i].name;
			setname(mkaux, &mkaux->oldfile, &child);
			if((!(d[i].mode&DMDIR) || rec) && copyfile(mkaux, &child, &d[i], 1) && rec)
				mktree(mkaux, &child, rec);
			free(child.new);
			if(child.old)
				free(child.old);
		}
	}
	close(fd);
}

static int
mkfile(Mkaux *mkaux, File *f)
{
	Dir *d;

	if((d = dirstat(mkaux->oldfile.s)) == nil){
		warn(mkaux, "can't stat file %s: %r", mkaux->oldfile.s);
		skipdir(mkaux);
		return 0;
	}
	return copyfile(mkaux, f, d, 0);
}

enum {
	SLOP = 30
};

static void
setname(Mkaux *mkaux, Name *name, File *f)
{
	char *s1, *s2, *ss;
	int l;
	
	s1 = mkaux->root;
	s2 = "";
	if(f->old){
		/* if old is not a absolute path, dont append root to it */
		if(f->old[0] != '/')
			s1 = f->old;
		else
			s2 = f->old;
	}else
		s2 = f->new;

	l = strlen(s1);
	ss = (*s1 && *s2 && *s2 != '/' && s1[l-1] != '/') ? "/" : "";
	l += strlen(ss);
	l += strlen(s2);
	l++;
	if(name->n < l+SLOP/2) {
		free(name->s);
		name->s = emalloc(mkaux, l+SLOP);
		name->n = l+SLOP;
	}
	snprint(name->s, name->n, "%s%s%s", s1, ss, s2);
}


static int
copyfile(Mkaux *mkaux, File *f, Dir *d, int permonly)
{
	Dir *nd;
	Opt *o;
	ulong xmode;
	char *p;

	setname(mkaux, &mkaux->fullname, f);

	/*
	 * Extra stat here is inefficient but accounts for binds.
	 */
	if((nd = dirstat(mkaux->fullname.s)) != nil)
		d = nd;

	d->name = f->elem;
	if(d->type != 'M'){
		d->uid = "sys";
		d->gid = "sys";
		xmode = (d->mode >> 6) & 7;
		d->mode |= xmode | (xmode << 3);
	}
	o = mkaux->opt;
	if(strcmp(f->uid, "-") != 0)
		d->uid = f->uid;
	else if(o && o->uid)
		d->uid = o->uid;
	if(strcmp(f->gid, "-") != 0)
		d->gid = f->gid;
	else if(o && o->gid)
		d->gid = o->gid;
	if(f->mode != ~0){
		if(permonly)
			d->mode = (d->mode & ~0666) | (f->mode & 0666);
		else if((d->mode&DMDIR) != (f->mode&DMDIR))
			warn(mkaux, "inconsistent mode for %s", f->new);
		else
			d->mode = f->mode;
	}

	if(p = strrchr(f->new, '/'))
		d->name = p+1;
	else
		d->name = f->new;
	mkaux->mkenum(f->new, mkaux->fullname.s, d, mkaux->a);
	xmode = d->mode;
	free(nd);
	return (xmode&DMDIR) != 0;
}

static char *
mkpath(Mkaux *mkaux, char *prefix, char *elem)
{
	char *p;
	int n;

	n = strlen(prefix) + strlen(elem) + 2;
	p = emalloc(mkaux, n);
	strcpy(p, prefix);
	strcat(p, "/");
	strcat(p, elem);
	return p;
}

static void
setopt(Mkaux *mkaux, char *key, char *val)
{
	Opt *o;

	o = mkaux->opt;
	if(o == nil || mkaux->indent > o->level){
		o = emalloc(mkaux, sizeof(*o));
		if(o == nil)
			longjmp(mkaux->jmp, 1);
		if(mkaux->opt){
			*o = *mkaux->opt;
			if(o->skip)
				o->skip = estrdup(mkaux, o->skip);
			if(o->uid)
				o->uid = estrdup(mkaux, o->uid);
			if(o->gid)
				o->gid = estrdup(mkaux, o->gid);
		}else
			memset(o, 0, sizeof(*o));
		o->level = mkaux->indent;
		o->prev = mkaux->opt;
		mkaux->opt = o;
	} else if(mkaux->indent < o->level)
		return;
	if(strcmp(key, "skip") == 0){
		free(o->skip); 
		o->skip = *val ? estrdup(mkaux, val) : nil;
	} else if(strcmp(key, "uid") == 0){
		free(o->uid); 
		o->uid = *val ? estrdup(mkaux, val) : nil;
	} else if(strcmp(key, "gid") == 0){
		free(o->gid); 
		o->gid = *val ? estrdup(mkaux, val) : nil;
	}
}

static void
popopt(Mkaux *mkaux)
{
	Opt *o;

	while(o = mkaux->opt){
		if(o->level <= mkaux->indent)
			break;
		mkaux->opt = o->prev;
		free(o->skip);
		free(o->uid);
		free(o->gid);
		free(o);
	}
}

static void
freefile(File *f)
{
	if(f->old)
		free(f->old);
	if(f->new)
		free(f->new);
	free(f);
}

/*
 * skip all files in the proto that
 * could be in the current dir
 */
static void
skipdir(Mkaux *mkaux)
{
	char *p, c;
	int level;

	if(mkaux->indent < 0)
		return;
	level = mkaux->indent;
	for(;;){
		mkaux->indent = 0;
		p = Brdline(mkaux->b, '\n');
		mkaux->lineno++;
		if(!p){
			mkaux->indent = -1;
			return;
		}
		while((c = *p++) != '\n')
			if(c == ' ')
				mkaux->indent++;
			else if(c == '\t')
				mkaux->indent += 8;
			else
				break;
		if(mkaux->indent <= level){
			popopt(mkaux);
			Bseek(mkaux->b, -Blinelen(mkaux->b), 1);
			mkaux->lineno--;
			return;
		}
	}
}

static File*
getfile(Mkaux *mkaux, File *old)
{
	File *f;
	char *elem;
	char *p, *s;
	int c;

	if(mkaux->indent < 0)
		return 0;
loop:
	mkaux->indent = 0;
	p = Brdline(mkaux->b, '\n');
	mkaux->lineno++;
	if(!p){
		mkaux->indent = -1;
		return 0;
	}
	while((c = *p++) != '\n')
		if(c == ' ')
			mkaux->indent++;
		else if(c == '\t')
			mkaux->indent += 8;
		else
			break;
	if(c == '\n' || c == '#')
		goto loop;
	p--;
	popopt(mkaux);
	*strchr(p, '\n') = 0;
	if(s = strchr(p, '=')){
		*s++ = 0;
		setopt(mkaux, p, s);
		goto loop;
	}else
		p[strlen(p)] = '\n';
	f = emalloc(mkaux, sizeof *f);
	p = getname(mkaux, p, &elem);
	if(p == nil)
		return nil;

	f->new = mkpath(mkaux, old->new, elem);
	free(elem);
	f->elem = utfrrune(f->new, L'/') + 1;
	p = getmode(mkaux, p, &f->mode);
	p = getname(mkaux, p, &f->uid);	/* LEAK */
	if(p == nil)
		return nil;

	if(!*f->uid)
		strcpy(f->uid, "-");
	p = getname(mkaux, p, &f->gid);	/* LEAK */
	if(p == nil)
		return nil;

	if(!*f->gid)
		strcpy(f->gid, "-");
	f->old = getpath(mkaux, p);
	if(f->old && strcmp(f->old, "-") == 0){
		free(f->old);
		f->old = 0;
	}
	setname(mkaux, &mkaux->oldfile, f);

	return f;
}

static char*
getpath(Mkaux *mkaux, char *p)
{
	char *q, *new;
	int c, n;

	while((c = *p) == ' ' || c == '\t')
		p++;
	q = p;
	while((c = *q) != '\n' && c != ' ' && c != '\t')
		q++;
	if(q == p)
		return 0;
	n = q - p;
	new = emalloc(mkaux, n + 1);
	memcpy(new, p, n);
	new[n] = 0;
	return new;
}

static char*
getname(Mkaux *mkaux, char *p, char **buf)
{
	char *s, *start;
	int c;

	while((c = *p) == ' ' || c == '\t')
		p++;

	start = p;
	while((c = *p) != '\n' && c != ' ' && c != '\t')
		p++;

	*buf = malloc(p+2-start);	/* +2: need at least 2 bytes; might strcpy "-" into buf */
	if(*buf == nil)
		return nil;
	memmove(*buf, start, p-start);

	(*buf)[p-start] = '\0';

	if(**buf == '$'){
		s = getenv(*buf+1);
		if(s == 0){
			warn(mkaux, "can't read environment variable %s", *buf+1);
			skipdir(mkaux);
			free(*buf);
			return nil;
		}
		free(*buf);
		*buf = s;
	}
	return p;
}

static char*
getmode(Mkaux *mkaux, char *p, ulong *xmode)
{
	char *buf, *s;
	ulong m;

	*xmode = ~0;
	p = getname(mkaux, p, &buf);
	if(p == nil)
		return nil;

	s = buf;
	if(!*s || strcmp(s, "-") == 0)
		return p;
	m = 0;
	if(*s == 'd'){
		m |= DMDIR;
		s++;
	}
	if(*s == 'a'){
		m |= DMAPPEND;
		s++;
	}
	if(*s == 'l'){
		m |= DMEXCL;
		s++;
	}
	if(s[0] < '0' || s[0] > '7'
	|| s[1] < '0' || s[1] > '7'
	|| s[2] < '0' || s[2] > '7'
	|| s[3]){
		warn(mkaux, "bad mode specification %s", buf);
		free(buf);
		return p;
	}
	*xmode = m | strtoul(s, 0, 8);
	free(buf);
	return p;
}

static void
warn(Mkaux *mkaux, char *fmt, ...)
{
	char buf[256];
	va_list va;

	va_start(va, fmt);
	vseprint(buf, buf+sizeof(buf), fmt, va);
	va_end(va);

	if(mkaux->warn)
		mkaux->warn(buf, mkaux->a);
	else
		fprint(2, "warning: %s\n", buf);
}
