#ifndef _ROBU_SYS_SYSINFO_H
#define _ROBU_SYS_SYSINFO_H

#ifdef __cplusplus
extern "C" {
#endif

struct sysinfo {
	long uptime;
	unsigned long loads[3];
	unsigned long totalram;
	unsigned long freeram;
	unsigned long sharedram;
	unsigned long bufferram;
	unsigned long totalswap;
	unsigned long freeswap;
	unsigned short procs;
	unsigned long totalhigh;
	unsigned long freehigh;
	unsigned int mem_unit;
	char _f[20 - 2 * sizeof(long) - sizeof(int)];
};

#define SI_LOAD_SHIFT 16

#ifndef __MLIBC_ABI_ONLY

int sysinfo(struct sysinfo *info);
int get_nprocs(void);
int get_nprocs_conf(void);

#endif

#ifdef __cplusplus
}
#endif

#endif
