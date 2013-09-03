#include <u.h>
#include <libc.h>
#include "../boot/boot.h"

char* bootdisk = "/dev/sdC0/";
extern void boot(int, char**);
void
main(int argc, char **argv)
{
	boot(argc, argv);
}
