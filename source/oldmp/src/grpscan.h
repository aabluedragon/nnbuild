#ifndef __grpscan_h__
#define __grpscan_h__

// List of internally-known GRP files
#define numgrpfiles 8
struct grpfile {
	const char *name;
	uint32_t crcval;
	int size;
	int game;
	struct grpfile *next;
};

extern grpfile grpfiles[numgrpfiles], *foundgrps;

int ScanGroups(void);
void FreeGroups(void);

#endif
