// Shared Duke3D/Redneck Rampage network menu definitions.
// The including menus.cpp supplies Menu* types, menu macros, fonts, ud, and menu IDs.

static char const *MEOSN_NetGametypes[MAXGAMETYPES];
static char const *MEOSN_NetEpisodes[MAXVOLUMES+1];
static int32_t MEOSV_NetEpisodes[MAXVOLUMES+1];
static char const *MEOSN_NetLevels[MAXVOLUMES][MAXLEVELS];
static char const *MEOSN_NetSkills[MAXSKILLS+1];

static MenuLink_t MEO_NETHOST_OPTIONS = { MENU_NETOPTIONS, MA_Advance, };
static MenuEntry_t ME_NETHOST_OPTIONS = MAKE_MENUENTRY( "Game Options", &MF_Redfont, &MEF_VideoSetup, &MEO_NETHOST_OPTIONS, Link );
static MenuEntry_t ME_NETHOST_LAUNCH = MAKE_MENUENTRY( "Launch Game", &MF_Redfont, &MEF_VideoSetup, &MEO_NULL, Link );

static MenuEntry_t *MEL_NETHOST[] = {
    &ME_NETHOST_OPTIONS,
    &ME_NETHOST_LAUNCH,
};

static MenuOptionSet_t MEOS_NETOPTIONS_GAMETYPE = MAKE_MENUOPTIONSET( MEOSN_NetGametypes, NULL, 0x0 );
static MenuOption_t MEO_NETOPTIONS_GAMETYPE = MAKE_MENUOPTION( &MF_Bluefont, &MEOS_NETOPTIONS_GAMETYPE, &ud.m_coop );
static MenuEntry_t ME_NETOPTIONS_GAMETYPE = MAKE_MENUENTRY( "Game Type", &MF_Redfont, &MEF_NetSetup, &MEO_NETOPTIONS_GAMETYPE, Option );
static MenuOptionSet_t MEOS_NETOPTIONS_EPISODE = MAKE_MENUOPTIONSET( MEOSN_NetEpisodes, MEOSV_NetEpisodes, 0x0 );
static int32_t NetEpisode;
static MenuOption_t MEO_NETOPTIONS_EPISODE = MAKE_MENUOPTION( &MF_Bluefont, &MEOS_NETOPTIONS_EPISODE, &NetEpisode );
static MenuEntry_t ME_NETOPTIONS_EPISODE = MAKE_MENUENTRY( "Episode", &MF_Redfont, &MEF_NetSetup, &MEO_NETOPTIONS_EPISODE, Option );
static MenuOptionSet_t MEOS_NETOPTIONS_LEVEL_TEMPLATE = MAKE_MENUOPTIONSETNULL;
static MenuOptionSet_t MEOS_NETOPTIONS_LEVEL[MAXVOLUMES];
static MenuOption_t MEO_NETOPTIONS_LEVEL = MAKE_MENUOPTION( &MF_Bluefont, NULL, &ud.m_level_number );
static MenuEntry_t ME_NETOPTIONS_LEVEL = MAKE_MENUENTRY( "Level", &MF_Redfont, &MEF_NetSetup, &MEO_NETOPTIONS_LEVEL, Option );
static MenuLink_t MEO_NETOPTIONS_USERMAP = { MENU_NETUSERMAP, MA_Advance, };
static MenuEntry_t ME_NETOPTIONS_USERMAP = MAKE_MENUENTRY( "User Map", &MF_Redfont, &MEF_NetSetup, &MEO_NETOPTIONS_USERMAP, Link );
static MenuOptionSet_t MEOS_NETOPTIONS_MONSTERS = MAKE_MENUOPTIONSET( MEOSN_NetSkills, NULL, 0x0 );
static MenuOption_t MEO_NETOPTIONS_MONSTERS = MAKE_MENUOPTION( &MF_Bluefont, &MEOS_NETOPTIONS_MONSTERS, NULL );
static MenuEntry_t ME_NETOPTIONS_MONSTERS = MAKE_MENUENTRY( "Monsters", &MF_Redfont, &MEF_NetSetup, &MEO_NETOPTIONS_MONSTERS, Option );
static MenuOption_t MEO_NETOPTIONS_MARKERS = MAKE_MENUOPTION( &MF_Bluefont, &MEOS_OffOn, &ud.m_marker );
static MenuEntry_t ME_NETOPTIONS_MARKERS = MAKE_MENUENTRY( "Markers", &MF_Redfont, &MEF_NetSetup, &MEO_NETOPTIONS_MARKERS, Option );
static MenuOption_t MEO_NETOPTIONS_MAPEXITS = MAKE_MENUOPTION( &MF_Bluefont, &MEOS_OnOff, &ud.m_noexits );
static MenuEntry_t ME_NETOPTIONS_MAPEXITS = MAKE_MENUENTRY( "Map Exits", &MF_Redfont, &MEF_NetSetup, &MEO_NETOPTIONS_MAPEXITS, Option );
static MenuOption_t MEO_NETOPTIONS_FRFIRE = MAKE_MENUOPTION( &MF_Bluefont, &MEOS_OffOn, &ud.m_ffire );
static MenuEntry_t ME_NETOPTIONS_FRFIRE = MAKE_MENUENTRY( "Fr. Fire", &MF_Redfont, &MEF_NetSetup, &MEO_NETOPTIONS_FRFIRE, Option );
static MenuEntry_t ME_NETOPTIONS_ACCEPT = MAKE_MENUENTRY( "Accept", &MF_Redfont, &MEF_NetSetup_Confirm, &MEO_NETWORK_HOSTGAME, Link );

static MenuEntry_t *MEL_NETOPTIONS[] = {
    &ME_NETOPTIONS_GAMETYPE,
    &ME_NETOPTIONS_EPISODE,
    &ME_NETOPTIONS_LEVEL,
    &ME_NETOPTIONS_MONSTERS,
    &ME_NETOPTIONS_MARKERS,
    &ME_NETOPTIONS_MAPEXITS,
    &ME_NETOPTIONS_ACCEPT,
};

static char MenuServer[BMAX_PATH] = "localhost";
static MenuString_t MEO_NETJOIN_SERVER = MAKE_MENUSTRING( MenuServer, &MF_Bluefont, BMAX_PATH, 0 );
static MenuEntry_t ME_NETJOIN_SERVER = MAKE_MENUENTRY( "Server", &MF_Redfont, &MEF_VideoSetup, &MEO_NETJOIN_SERVER, String );
#define MAXPORTSTRINGLENGTH 6 // unsigned 16-bit integer
static char MenuPort[MAXPORTSTRINGLENGTH] = "19014";
static MenuString_t MEO_NETJOIN_PORT = MAKE_MENUSTRING( MenuPort, &MF_Bluefont, MAXPORTSTRINGLENGTH, INPUT_NUMERIC );
static MenuEntry_t ME_NETJOIN_PORT = MAKE_MENUENTRY( "Port", &MF_Redfont, &MEF_VideoSetup, &MEO_NETJOIN_PORT, String );
static MenuEntry_t ME_NETJOIN_CONNECT = MAKE_MENUENTRY( "Connect", &MF_Redfont, &MEF_VideoSetup_Apply, &MEO_NULL, Link );

static MenuEntry_t *MEL_NETJOIN[] = {
    &ME_NETJOIN_SERVER,
    &ME_NETJOIN_PORT,
    &ME_NETJOIN_CONNECT,
};
