#ifndef __kexmenu_h__
#define __kexmenu_h__

// VibeBuild32: the Nightdive PowerSlave Exhumed menu, drawn with the remaster's
// own art. Available only when that data is mounted (-kex); otherwise the
// classic Build PowerSlave menu is used unchanged.
int  KexMenu_Available(void);
int  KexMenu_Main(int bInLevelMenus);      // same return codes as menu_Menu()
int  KexHud_Available(void);
void KexHud_Draw(void);                    // the remaster's status bar
void KexMenu_TitleHold(void);              // PRESS START hold, after the cinematic
void KexMenu_Status(bool weaponWheel = false); // inventory, or the KEX weapon selector

// Overworld level select: walk the remaster's map with the arrow keys. Returns
// the chosen level's index, or -1 to go back to the menu.
int  KexMenu_Overworld(int start);
int  KexMenu_LevelCount(void);
const char *KexMenu_LevelMap(int i);
const char *KexMenu_ChosenMap(void);       // level picked on the overworld, or NULL
const char *KexMenu_ChooseNextMap(const char *current); // return to the overworld from a level exit
void KexMenu_Backdrop(void);
void KexMenu_Text(int x, int y, const char *str, int height, int shade, int pal, int centred, int altFont = 0);
int  KexMenu_TextWidth(const char *str, int height);

// The remaster can use the original console overworld painting and cursor.
// This setting is read only by the KEX menu path.
extern int kex_original_overworld;

#endif
