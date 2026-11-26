#ifndef GAME_STAT_FLAGS_SHARED_H_
#define GAME_STAT_FLAGS_SHARED_H_

// rotatesprite flags
#define ROTATE_SPRITE_TRANSLUCENT   (BIT(0))
#define ROTATE_SPRITE_VIEW_CLIP     (BIT(1)) // clip to view
#define ROTATE_SPRITE_YFLIP         (BIT(2))
#define ROTATE_SPRITE_IGNORE_START_MOST (BIT(3)) // don't clip to startumost
#define ROTATE_SPRITE_SCREEN_CLIP   (BIT(1)|BIT(3)) // use window
#define ROTATE_SPRITE_CORNER        (BIT(4)) // place sprite from upper left corner
#define ROTATE_SPRITE_TRANS_FLIP    (BIT(5))
#define ROTATE_SPRITE_NON_MASK      (BIT(6)) // non masked sprites
#define ROTATE_SPRITE_ALL_PAGES     (BIT(7)) // copies to all pages

#define RS_SCALE                    BIT(16)

// system defines for status bits
#define CEILING_STAT_PLAX           BIT(0)
#define CEILING_STAT_SLOPE          BIT(1)
#define CEILING_STAT_SWAPXY         BIT(2)
#define CEILING_STAT_SMOOSH         BIT(3)
#define CEILING_STAT_XFLIP          BIT(4)
#define CEILING_STAT_YFLIP          BIT(5)
#define CEILING_STAT_RELATIVE       BIT(6)
#define CEILING_STAT_TYPE_MASK     (BIT(7)|BIT(8))
#define CEILING_STAT_MASKED         BIT(7)
#define CEILING_STAT_TRANS          BIT(8)
#define CEILING_STAT_TRANS_FLIP     (BIT(7)|BIT(8))
#define CEILING_STAT_FAF_BLOCK_HITSCAN      BIT(15)

#define FLOOR_STAT_PLAX           BIT(0)
#define FLOOR_STAT_SLOPE          BIT(1)
#define FLOOR_STAT_SWAPXY         BIT(2)
#define FLOOR_STAT_SMOOSH         BIT(3)
#define FLOOR_STAT_XFLIP          BIT(4)
#define FLOOR_STAT_YFLIP          BIT(5)
#define FLOOR_STAT_RELATIVE       BIT(6)
#define FLOOR_STAT_TYPE_MASK     (BIT(7)|BIT(8))
#define FLOOR_STAT_MASKED         BIT(7)
#define FLOOR_STAT_TRANS          BIT(8)
#define FLOOR_STAT_TRANS_FLIP     (BIT(7)|BIT(8))
#define FLOOR_STAT_FAF_BLOCK_HITSCAN      BIT(15)

#endif
