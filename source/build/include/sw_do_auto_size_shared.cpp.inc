void
DoAutoSize(tspriteptr_t tspr)
{
    if (!bAutoSize)
        return;

    switch (tspr->picnum)
    {
    case ICON_STAR:                     // 1793
        break;
    case ICON_UZI:                      // 1797
        tspr->xrepeat = 43;
        tspr->yrepeat = 40;
        break;
    case ICON_UZIFLOOR:         // 1807
        tspr->xrepeat = 43;
        tspr->yrepeat = 40;
        break;
    case ICON_LG_UZI_AMMO:              // 1799
        break;
    case ICON_HEART:                    // 1824
        break;
    case ICON_HEART_LG_AMMO:            // 1820
        break;
    case ICON_GUARD_HEAD:               // 1814
        break;
    case ICON_FIREBALL_LG_AMMO: // 3035
        break;
    case ICON_ROCKET:                   // 1843
        break;
    case ICON_SHOTGUN:                  // 1794
        tspr->xrepeat = 57;
        tspr->yrepeat = 58;
        break;
    case ICON_LG_ROCKET:                // 1796
        break;
    case ICON_LG_SHOTSHELL:             // 1823
        break;
    case ICON_MICRO_GUN:                // 1818
        break;
    case ICON_MICRO_BATTERY:            // 1800
        break;
    case ICON_GRENADE_LAUNCHER: // 1817
        tspr->xrepeat = 54;
        tspr->yrepeat = 52;
        break;
    case ICON_LG_GRENADE:               // 1831
        break;
    case ICON_LG_MINE:                  // 1842
        break;
    case ICON_RAIL_GUN:         // 1811
        tspr->xrepeat = 50;
        tspr->yrepeat = 54;
        break;
    case ICON_RAIL_AMMO:                // 1812
        break;
    case ICON_SM_MEDKIT:                // 1802
        break;
    case ICON_MEDKIT:                   // 1803
        break;
    case ICON_CHEMBOMB:
        tspr->xrepeat = 64;
        tspr->yrepeat = 47;
        break;
    case ICON_FLASHBOMB:
        tspr->xrepeat = 32;
        tspr->yrepeat = 34;
        break;
    case ICON_NUKE:
        break;
    case ICON_CALTROPS:
        tspr->xrepeat = 37;
        tspr->yrepeat = 30;
        break;
    case ICON_BOOSTER:                  // 1810
        tspr->xrepeat = 30;
        tspr->yrepeat = 38;
        break;
    case ICON_HEAT_CARD:                // 1819
        tspr->xrepeat = 46;
        tspr->yrepeat = 47;
        break;
    case ICON_REPAIR_KIT:               // 1813
        break;
    case ICON_EXPLOSIVE_BOX:            // 1801
        break;
    case ICON_ENVIRON_SUIT:             // 1837
        break;
    case ICON_FLY:                      // 1782
        break;
    case ICON_CLOAK:                    // 1826
        break;
    case ICON_NIGHT_VISION:             // 3031
        tspr->xrepeat = 59;
        tspr->yrepeat = 71;
        break;
    case ICON_NAPALM:                   // 3046
        break;
    case ICON_RING:                     // 3050
        break;
    case ICON_RINGAMMO:         // 3054
        break;
    case ICON_NAPALMAMMO:               // 3058
        break;
    case ICON_GRENADE:                  // 3059
        break;
    case ICON_ARMOR:                    // 3030
        tspr->xrepeat = 82;
        tspr->yrepeat = 84;
        break;
    case BLUE_KEY:                      // 1766
        break;
    case RED_KEY:                       // 1770
        break;
    case GREEN_KEY:                     // 1774
        break;
    case YELLOW_KEY:                    // 1778
        break;
    case BLUE_CARD:
    case RED_CARD:
    case GREEN_CARD:
    case YELLOW_CARD:
        tspr->xrepeat = 36;
        tspr->yrepeat = 33;
        break;
    case GOLD_SKELKEY:
    case SILVER_SKELKEY:
    case BRONZE_SKELKEY:
    case RED_SKELKEY:
        tspr->xrepeat = 39;
        tspr->yrepeat = 45;
        break;
    case SKEL_LOCKED:
    case SKEL_UNLOCKED:
        tspr->xrepeat = 47;
        tspr->yrepeat = 40;
        break;
    case RAMCARD_LOCKED:
    case RAMCARD_UNLOCKED:
    case CARD_LOCKED:
    case CARD_UNLOCKED:
        break;
    default:
        break;
    }
}
