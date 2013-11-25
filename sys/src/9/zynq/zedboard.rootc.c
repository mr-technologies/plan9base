
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"

extern uchar boot_CONF_outcode[];
extern ulong boot_CONF_outlen;
extern uchar __objtype_bin_paqfscode[];
extern ulong __objtype_bin_paqfslen;
extern uchar __objtype_bin_auth_factotumcode[];
extern ulong __objtype_bin_auth_factotumlen;
extern uchar bootfs_paqcode[];
extern ulong bootfs_paqlen;

void bootlinks(void){

	addbootfile("boot", boot_CONF_outcode, boot_CONF_outlen);
	addbootfile("paqfs", __objtype_bin_paqfscode, __objtype_bin_paqfslen);
	addbootfile("factotum", __objtype_bin_auth_factotumcode, __objtype_bin_auth_factotumlen);
	addbootfile("bootfs.paq", bootfs_paqcode, bootfs_paqlen);

}

