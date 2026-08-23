//-------------------------------------------------------------------------
//
// The remaster's breakable props for VibeBuild32.
//
// Vases, barrels and the like are shootable actors in the Kex engine: the
// actor def gives them health and a deathAnim, and that anim tosses one shard
// actor per shard, plays a break sound and destroys the prop. None of that
// survives conversion on its own -- the converter emits them as plain decor
// sprites, which stand there and ignore bullets.
//
//-------------------------------------------------------------------------
#ifndef __kexbreak_h__
#define __kexbreak_h__

int  KexBreak_Available(void);   // the table loaded and this is a Kex map
void KexBreak_Init(void);        // per level: arm every breakable prop sprite
void FuncKexBreak(int a, int b, int nRun);

#endif
