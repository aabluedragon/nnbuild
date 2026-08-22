#ifndef __kexmenu_h__
#define __kexmenu_h__

// VibeBuild32: the Nightdive PowerSlave Exhumed menu, drawn with the remaster's
// own art. Available only when that data is mounted (-kex); otherwise the
// classic Build PowerSlave menu is used unchanged.
int  KexMenu_Available(void);
int  KexMenu_Main(int bInLevelMenus);      // same return codes as menu_Menu()
void KexMenu_TitleHold(void);              // PRESS START hold, after the cinematic
void KexMenu_Backdrop(void);
void KexMenu_Text(int x, int y, const char *str, int height, int shade, int pal, int centred, int altFont = 0);
int  KexMenu_TextWidth(const char *str, int height);

#endif
