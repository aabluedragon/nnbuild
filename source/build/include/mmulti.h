// "Build Engine & Tools" Copyright (c) 1993-1997 Ken Silverman
// Ken Silverman's official web site: "http://www.advsys.net/ken"
// See the included license file "BUILDLIC.TXT" for license info.
//
// This file has been modified from Ken Silverman's original release
// by Jonathon Fowler (jf@jonof.id.au)
// by the EDuke32 team (development@voidpoint.com)

#ifndef mmulti_h_
#define mmulti_h_

#define MAXMULTIPLAYERS 16

enum { MMULTI_MODE_MS = 0, MMULTI_MODE_P2P };

extern int myconnectindex, numplayers, networkmode;
extern int connecthead, connectpoint2[MAXMULTIPLAYERS];
extern char syncstate;
extern int natfree; //Addfaz NatFree

int initmultiplayersparms(int argc, const char * const *argv);
int initmultiplayerscycle(void);

void mmulti_initmultiplayers(int argc, const char * const *argv, char damultioption, char dacomrateoption, char dapriority);
void mmulti_allowlatejoin(int minplayers);
int mmulti_slotconnected(int slot);
void mmulti_setcallbacks(int (*bancheck)(int ip), void (*joinnotify)(int ip), int (*reconnectnotify)(int slot));
int mmulti_getslotip(int slot);
// Full textual address of a slot ("a.b.c.d:port" or "[ipv6]:port"); "" if empty.
void mmulti_getslotaddrstr(int slot, char *out, int outsize);
int mmulti_getport(void);
// "Are you there?" A player who can reach a host over IPv4 and over IPv6 asks
// over both before choosing one. The question is MMULTI_PROBE_QUERY followed by
// four bytes of the asker's choosing (8 bytes in all). The host answers from its
// game socket with MMULTI_PROBE_REPLY, the same four bytes, and one byte holding
// the number of free player slots (9 bytes). No player slot is used, and the
// answer is no larger than the question.
#define MMULTI_PROBE_QUERY "NBPQ"
#define MMULTI_PROBE_REPLY "NBPR"

// Open a NAT / firewall hole toward addr ("a.b.c.d:port" or "[ipv6]:port").
void mmulti_punchto_str(const char *addr);
// Send raw bytes from the game socket to addr (v4 or v6) - used to deliver
// masterserver NAT-punch requests from the real game endpoint.
void mmulti_sendraw_str(const char *addr, const void *buf, int len);
void mmulti_dropslot(int slot);
// Also service (send acks/retransmits to, and deliver ordered packets from) a
// slot that isn't in the active connect list yet - used so the host can talk
// reliably with a late joiner during the state transfer, before it's folded
// into the running game. Pass -1 to clear.
void mmulti_setextraslot(int slot);
void mmulti_setpackettimeout(int datimeoutcount, int daresendagaincount);
void mmulti_uninitmultiplayers(void);
void mmulti_sendlogon(void);
void mmulti_sendlogoff(void);
int mmulti_getoutputcirclesize(void);
void mmulti_sendpacket(int other, unsigned char *bufptr, int messleng);
int mmulti_getpacket(int *other, unsigned char *bufptr);
int mmulti_getpacket_sized(int *other, unsigned char *bufptr, int capacity);
void mmulti_flushpackets(void);
void mmulti_generic(int other, unsigned char *bufptr, int messleng, int command);
int isvalidipaddress(const char *st);

void nfIncCP(void); //Addfaz NatFree
int nfCheckHF(int other); //Addfaz NatFree
int nfCheckCP(int other); //Addfaz NatFree

#define initmultiplayers mmulti_initmultiplayers
#define sendpacket mmulti_sendpacket
#define setpackettimeout mmulti_setpackettimeout
#define uninitmultiplayers mmulti_uninitmultiplayers
#define sendlogon mmulti_sendlogon
#define sendlogoff mmulti_sendlogoff
#define getoutputcirclesize mmulti_getoutputcirclesize
#define getpacket mmulti_getpacket
#define flushpackets mmulti_flushpackets
#define genericmultifunction mmulti_generic

#endif  // mmulti_h_
