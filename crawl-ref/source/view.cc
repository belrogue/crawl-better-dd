/*
 *  File:       view.cc
 *  Summary:    Misc function used to render the dungeon.
 *  Written by: Linley Henzell
 *
 *  Modified for Crawl Reference by $Author$ on $Date$
 *
 *  Change History (most recent first):
 *
 *   <10>     29 Jul 00  JDJ    show_map iterates horizontally to 79 instead of 80.
 *                              item no longer indexes past the end of environ::grid.
 *   <9>      19 Jun 00  GDL    Complete rewrite of LOS code
 *   <8>      11/23/99   LRH    Added colour-coded play-screen map & clean_map
 *                                                              init options
 *   <7>      9/29/99    BCR    Removed first argument from draw_border
 *   <6>      9/11/99    LRH    Added calls to overmap functions
 *   <5>      6/22/99    BWR    Fixed and improved the stealth
 *   <4>      5/20/99    BWR    show_map colours all portals,
 *                              exits from subdungeons now
 *                              look like up stairs.
 *   <3>      5/09/99    JDJ    show_map draws shops in yellow.
 *   <2>      5/09/99    JDJ    show_map accepts '\r' along with '.'.
 *   <1>      -/--/--    LRH    Created
 */

#include "AppHdr.h"
#include "view.h"

#include <string.h>

#ifdef DOS
#include <conio.h>
#endif

#include "externs.h"

#include "command.h"
#include "clua.h"
#include "debug.h"
#include "delay.h"
#include "direct.h"
#include "initfile.h"
#include "insult.h"
#include "itemprop.h"
#include "macro.h"
#include "misc.h"
#include "monstuff.h"
#include "mon-util.h"
#include "overmap.h"
#include "player.h"
#include "skills2.h"
#include "stuff.h"
#include "spells4.h"
#include "stash.h"
#include "travel.h"

// These are hidden from the rest of the world... use the functions
// below to get information about the map grid.
#define MAP_MAGIC_MAPPED_FLAG   0x0100
#define MAP_SEEN_FLAG           0x0200
#define MAP_CHANGED_FLAG        0x0400
#define MAP_DETECTED_MONSTER    0x0800
#define MAP_DETECTED_ITEM       0x1000

#define MAP_CHARACTER_MASK      0x00ff

// Flags that define what the player remembers on this square.
#define MC_ITEM             0x01
#define MC_MONS             0x02

static FixedVector<feature_def, NUM_FEATURES> Feature;

#if defined(DOS_TERM)
// DOS functions like gettext() and puttext() can only
// work with arrays of characters, not shorts.
typedef unsigned char screen_buffer_t;
#else
typedef unsigned short screen_buffer_t;
#endif

FixedArray < unsigned int, 20, 19 > Show_Backup;

unsigned char show_green;
extern int stealth;             // defined in acr.cc

// char colour_code_map(unsigned char map_value);
screen_buffer_t colour_code_map( int x, int y, bool item_colour = false,
                                    bool travel_colour = false );

void cloud_grid(void);
void monster_grid(bool do_updates);

static int get_item_dngn_code(const item_def &item);
static void set_show_backup( int ex, int ey );

// Applies EC_ colour substitutions and brands.
static unsigned fix_colour(unsigned raw_colour);
static int get_viewobj_flags(int viewobj);

//---------------------------------------------------------------
//
// get_number_of_lines
//
// Made this a function instead of a #define.  This should help
// considering the fact that the curses version is a macro 
// (curses tends to be implemented with a large number of 
// preprocessor macros, which can wreak havoc with things
// like the C++ string class, so we want to isolate that
// away to keep portability up). 
//
// Other OSes might want to hook into reading system environment 
// variables or player set options to determine the screen size 
// (see the Options and SysEnv structures, as well as initfile.cc).
//
// This might be better to move to the lib*.cc files, but we
// don't really have a standard API defined for them, or the
// all important libdos.cc.  It would be a good idea to eventually
// head that way. -- bwr
//
//---------------------------------------------------------------
int get_number_of_lines(void)
{
#ifdef UNIX
    return (get_number_of_lines_from_curses());
#else
    return (25);
#endif
}

int get_number_of_cols(void)
{
#ifdef UNIX
    return (get_number_of_cols_from_curses());
#else
    return (80);
#endif
}

unsigned get_envmap_char(int x, int y)
{
    return static_cast<unsigned char>(
            env.map[x - 1][y - 1] & MAP_CHARACTER_MASK);
}

void set_envmap_detected_item(int x, int y, bool detected)
{
    if (detected)
        env.map[x - 1][y - 1] |= MAP_DETECTED_ITEM;
    else
        env.map[x - 1][y - 1] &= ~MAP_DETECTED_ITEM;
}

bool is_envmap_detected_item(int x, int y)
{
    return (env.map[x - 1][y - 1] & MAP_DETECTED_ITEM);
}

void set_envmap_detected_mons(int x, int y, bool detected)
{
    if (detected)
        env.map[x - 1][y - 1] |= MAP_DETECTED_MONSTER;
    else
        env.map[x - 1][y - 1] &= ~MAP_DETECTED_MONSTER;
}

bool is_envmap_detected_mons(int x, int y)
{
    return (env.map[x - 1][y - 1] & MAP_DETECTED_MONSTER);
}

void set_envmap_glyph(int x, int y, int chr, int col, int object)
{
    set_envmap_char(x, y, chr);
    set_envmap_col(x, y, col, get_viewobj_flags(object));
}

void set_envmap_char( int x, int y, unsigned char chr )
{
    env.map[x - 1][y - 1] &= (~MAP_CHARACTER_MASK);       // clear old first
    env.map[x - 1][y - 1] |= chr;
}

void set_envmap_col( int x, int y, int colour )
{
    env.map_col[x - 1][y - 1].colour = colour;
    env.map_col[x - 1][y - 1].flags  = 0;
}

void set_envmap_col( int x, int y, int colour, int flags )
{
    env.map_col[x - 1][y - 1].colour = colour;
    env.map_col[x - 1][y - 1].flags  = flags;
}

inline void set_envmap_item(int x, int y, bool isitem)
{
    if (isitem)
        env.map_col[x - 1][y - 1].flags |= MC_ITEM;
    else
        env.map_col[x - 1][y - 1].flags &= ~MC_ITEM;
}

inline void set_envmap_mons(int x, int y, bool ismons)
{
    if (ismons)
        env.map_col[x - 1][y - 1].flags |= MC_MONS;
    else
        env.map_col[x - 1][y - 1].flags &= ~MC_MONS;
}

bool is_envmap_item(int x, int y)
{
    return (env.map_col[x - 1][y - 1].flags & MC_ITEM);
}

bool is_envmap_mons(int x, int y)
{
    return (env.map_col[x - 1][y - 1].flags & MC_MONS);
}

int get_envmap_col(int x, int y)
{
    return (env.map_col[x - 1][y - 1].colour);
}

bool is_terrain_known( int x, int y )
{
    return (env.map[x - 1][y - 1] & (MAP_MAGIC_MAPPED_FLAG | MAP_SEEN_FLAG));
}

bool is_terrain_seen( int x, int y )
{
    return (env.map[x - 1][y - 1] & MAP_SEEN_FLAG);
}

bool is_terrain_changed( int x, int y )
{
    return (env.map[x - 1][y - 1] & MAP_CHANGED_FLAG);
}

// used to mark dug out areas, unset when terrain is seen or mapped again.
void set_terrain_changed( int x, int y )
{
    env.map[x - 1][y - 1] |= MAP_CHANGED_FLAG;
}

void set_terrain_mapped( int x, int y )
{
    env.map[x - 1][y - 1] &= (~MAP_CHANGED_FLAG);
    env.map[x - 1][y - 1] |= MAP_MAGIC_MAPPED_FLAG;
}

void set_terrain_seen( int x, int y )
{
    env.map[x - 1][y - 1] &= (~MAP_CHANGED_FLAG);
    env.map[x - 1][y - 1] |= MAP_SEEN_FLAG;
}

void clear_envmap_grid( int x, int y )
{
    env.map[x - 1][y - 1] = 0;
    env.map_col[x - 1][y - 1].clear();
}

void clear_envmap( void )
{
    for (int i = 0; i < GXM; i++)
    {
        for (int j = 0; j < GYM; j++)
        {
            env.map[i][j] = 0;
            env.map_col[i][j].clear();
        }
    }
}

#if defined(WIN32CONSOLE) || defined(DOS) || defined(DOS_TERM)
static unsigned colflag2brand(int colflag)
{
    switch (colflag)
    {
    case COLFLAG_ITEM_HEAP:
        return (Options.heap_brand);
    case COLFLAG_FRIENDLY_MONSTER:
        return (Options.friend_brand);
    case COLFLAG_WILLSTAB:
        return (Options.stab_brand);
    case COLFLAG_MAYSTAB:
        return (Options.may_stab_brand);
    default:
        return (CHATTR_NORMAL);
    }
}
#endif

static unsigned fix_colour(unsigned raw_colour)
{
    // This order is important - is_element_colour() doesn't want to see the
    // munged colours returned by dos_brand, so it should always be done 
    // before applying DOS brands.
#if defined(WIN32CONSOLE) || defined(DOS) || defined(DOS_TERM)
    const int colflags = raw_colour & 0xFF00;
#endif

    // Evaluate any elemental colours to guarantee vanilla colour is returned
    if (is_element_colour( raw_colour ))
        raw_colour = element_colour( raw_colour );

#if defined(WIN32CONSOLE) || defined(DOS) || defined(DOS_TERM)
    if (colflags)
    {
        unsigned brand = colflag2brand(colflags);
        raw_colour = dos_brand(raw_colour & 0xFF, brand);
    }
#endif

#ifndef USE_COLOUR_OPTS
    // Strip COLFLAGs for systems that can't do anything meaningful with them.
    raw_colour &= 0xFF;
#endif

    return (raw_colour);
}

static int get_viewobj_flags(int object)
{
    // Check for monster glyphs.
    if (object >= DNGN_START_OF_MONSTERS)
        return (MC_MONS);

    // Check for item glyphs.
    if (object >= DNGN_ITEM_ORB && object < DNGN_CLOUD)
        return (MC_ITEM);

    // We don't care to look further; we could check for
    // clouds here as well.
    return (0);
}

static void get_symbol( int x, int y,
                        unsigned int object, unsigned short *ch, 
                        unsigned short *colour )
{
    ASSERT( ch != NULL );
    ASSERT( colour != NULL );

    if (object < NUM_FEATURES)
    {
        *ch = Feature[object].symbol;    

        // Don't clobber with BLACK, because the colour should be already set.
        if (Feature[object].colour != BLACK)
            *colour = Feature[object].colour;

        // Note anything we see that's notable
        if (Feature[object].notable)
            seen_notable_thing( object, x, y );
    }
    else
    {
        ASSERT( object >= DNGN_START_OF_MONSTERS );
        *ch = mons_char( object - DNGN_START_OF_MONSTERS );
    }

    *colour = fix_colour(*colour);
}

unsigned char get_sightmap_char( int feature )
{
    if (feature < NUM_FEATURES)
        return (Feature[feature].symbol);

    return (0);
}

unsigned char get_magicmap_char( int feature )
{
    if (feature < NUM_FEATURES)
        return (Feature[feature].magic_symbol);

    return (0);
}

static char get_travel_colour( int x, int y )
{
    if (is_waypoint(x + 1, y + 1))
        return LIGHTGREEN;

    short dist = point_distance[x + 1][y + 1];
    return dist > 0?                    Options.tc_reachable        :
           dist == PD_EXCLUDED?         Options.tc_excluded         :
           dist == PD_EXCLUDED_RADIUS?  Options.tc_exclude_circle   :
           dist < 0?                    Options.tc_dangerous        :
                                        Options.tc_disconnected;
}
            
#if defined(WIN32CONSOLE) || defined(DOS)
static unsigned short dos_reverse_brand(unsigned short colour)
{
    if (Options.dos_use_background_intensity)
    {
        // If the console treats the intensity bit on background colours
        // correctly, we can do a very simple colour invert.

        // Special casery for shadows.
        if (colour == BLACK)
            colour = (DARKGREY << 4);
        else
            colour = (colour & 0xF) << 4;
    }
    else
    {
        // If we're on a console that takes its DOSness very seriously the
        // background high-intensity bit is actually a blink bit. Blinking is
        // evil, so we strip the background high-intensity bit. This, sadly,
        // limits us to 7 background colours.
        
        // Strip off high-intensity bit.  Special case DARKGREY, since it's the
        // high-intensity counterpart of black, and we don't want black on
        // black.
        //
        // We *could* set the foreground colour to WHITE if the background
        // intensity bit is set, but I think we've carried the
        // angry-fruit-salad theme far enough already.

        if (colour == DARKGREY)
            colour |= (LIGHTGREY << 4);
        else if (colour == BLACK)
            colour = LIGHTGREY << 4;
        else
        {
            // Zap out any existing background colour, and the high
            // intensity bit.
            colour  &= 7;

            // And swap the foreground colour over to the background
            // colour, leaving the foreground black.
            colour <<= 4;
        }
    }

    return (colour);
}

static unsigned short dos_hilite_brand(unsigned short colour,
                                       unsigned short hilite)
{
    if (!hilite)
        return (colour);

    if (colour == hilite)
        colour = 0;

    colour |= (hilite << 4);
    return (colour);
}

unsigned short dos_brand( unsigned short colour,
                          unsigned brand) 
{
    if ((brand & CHATTR_ATTRMASK) == CHATTR_NORMAL)
        return (colour);

    colour &= 0xFF;
    
    if ((brand & CHATTR_ATTRMASK) == CHATTR_HILITE)
        return dos_hilite_brand(colour, (brand & CHATTR_COLMASK) >> 8);
    else
        return dos_reverse_brand(colour);
    
}
#endif

// FIXME: Rework this function to use the new terrain known/seen checks
// These are still env.map coordinates, NOT grid coordinates!
screen_buffer_t colour_code_map( int x, int y, bool item_colour, 
                                 bool travel_colour )
{
    // XXX: Yes, the map array and the grid array are off by one. -- bwr
    const unsigned short map_flags = env.map[x][y];
    const int grid_value = grd[x + 1][y + 1];

    unsigned tc = travel_colour? 
                        get_travel_colour(x, y)
                      : DARKGREY;

    if (map_flags & MAP_DETECTED_ITEM)
        tc = Options.detected_item_colour;
    
    if (map_flags & MAP_DETECTED_MONSTER)
    {
        tc = Options.detected_monster_colour;
        return fix_colour(tc);
    }

    // XXX: [ds] If we've an important colour, override other feature
    // colouring. Yes, this is hacky. Story of my life.
    if (tc == LIGHTGREEN || tc == LIGHTMAGENTA)
        return fix_colour(tc);

    if (item_colour && is_envmap_item(x + 1, y + 1))
        return get_envmap_col(x + 1, y + 1);

    int feature_colour = DARKGREY;
    feature_colour = 
        is_terrain_seen(x + 1, y + 1)? Feature[grid_value].seen_colour 
                                     : Feature[grid_value].map_colour;

    if (feature_colour != DARKGREY)
        tc = feature_colour;

    return fix_colour(tc);
}

void clear_map()
{
    for (int y = Y_BOUND_1; y <= Y_BOUND_2; ++y)
    {
        for (int x = X_BOUND_1; x <= X_BOUND_2; ++x)
        {
            // Don't expose new dug out areas:
            // Note: assumptions are being made here about how 
            // terrain can change (eg it used to be solid, and
            // thus monster/item free).
            if (is_terrain_changed(x, y))
                continue;

            unsigned short envc = get_envmap_char(x, y);
            if (!envc)
                continue;

            if (is_envmap_item(x, y))
                continue;

            set_envmap_char(x, y,
                    is_terrain_seen(x, y)? get_sightmap_char(grd[x][y]) :
                    is_terrain_known(x, y)? get_magicmap_char(grd[x][y]) :
                                            0);
            set_envmap_detected_mons(x, y, false);
        }
    }
}

void monster_grid(bool do_updates)
{
    struct monsters *monster = 0;       // NULL {dlb}

    for (int s = 0; s < MAX_MONSTERS; s++)
    {
        monster = &menv[s];

        if (monster->type != -1 && mons_near(monster))
        {
            if (do_updates 
                && (monster->behaviour == BEH_SLEEP
                     || monster->behaviour == BEH_WANDER) 
                && check_awaken(s))
            {
                behaviour_event( monster, ME_ALERT, MHITYOU );

                if (you.turn_is_over
                    && mons_shouts(monster->type) > 0
                    && random2(30) >= you.skills[SK_STEALTH])
                {
                    int noise_level = 8; 

                    if (!mons_friendly(monster)
                        && (!silenced(you.x_pos, you.y_pos) 
                            && !silenced(monster->x, monster->y)))
                    {
                        if (mons_is_demon( monster->type ) && coinflip())
                        {
                            if (monster->type == MONS_IMP
                                || monster->type == MONS_WHITE_IMP
                                || monster->type == MONS_SHADOW_IMP)
                            {
                                imp_taunt( monster );
                            }
                            else
                            {
                                demon_taunt( monster );
                            }
                        }
                        else
                        {
                            int the_shout = mons_shouts(monster->type);

                            strcpy(info, "You hear ");
                            switch (the_shout)
                            {
                            case S_SILENT:
                            default:
                                strcat(info, "buggy behaviour!");
                                break;
                            case S_SHOUT:
                                strcat(info, "a shout!");
                                break;
                            case S_BARK:
                                strcat(info, "a bark!");
                                break;
                            case S_SHOUT2:
                                strcat(info, "two shouts!");
                                noise_level = 12;
                                break;
                            case S_ROAR:
                                strcat(info, "a roar!");
                                noise_level = 12;
                                break;
                            case S_SCREAM:
                                strcat(info, "a hideous shriek!");
                                break;
                            case S_BELLOW:
                                strcat(info, "a bellow!");
                                break;
                            case S_SCREECH:
                                strcat(info, "a screech!");
                                break;
                            case S_BUZZ:
                                strcat(info, "an angry buzzing noise.");
                                break;
                            case S_MOAN:
                                strcat(info, "a chilling moan.");
                                break;
                            case S_WHINE:
                                strcat(info,
                                       "an irritating high-pitched whine.");
                                break;
                            case S_CROAK:
                                if (coinflip())
                                    strcat(info, "a loud, deep croak!");
                                else
                                    strcat(info, "a croak.");
                                break;
                            case S_GROWL:
                                strcat(info, "an angry growl!");
                                break;
                            case S_HISS:
                                strcat(info, "an angry hiss!");
                                noise_level = 4;  // not very loud -- bwr
                                break;
                            }

                            mpr(info);
                        }
                    }

                    noisy( noise_level, monster->x, monster->y );
                }
            }

            const int ex = monster->x - you.x_pos + 9;
            const int ey = monster->y - you.y_pos + 9;

            if (!player_monster_visible( monster ))
            {
                // ripple effect?
                if (grd[monster->x][monster->y] == DNGN_SHALLOW_WATER
                    && !mons_flies(monster))
                {
                    set_show_backup(ex, ey);
                    env.show[ex][ey] = DNGN_INVIS_EXPOSED;
                    env.show_col[ex][ey] = BLUE; 
                }
                continue;
            }
            else if (!do_updates
                     && !mons_friendly( monster )
                     && !mons_is_mimic( monster->type )
                     && !mons_class_flag( monster->type, M_NO_EXP_GAIN ))
            {
                interrupt_activity( AI_SEE_MONSTER, monster );
                seen_monster( monster );
            }

            // mimics are always left on map
            if (!mons_is_mimic( monster->type ))
                set_show_backup(ex, ey);

            env.show[ex][ey] = monster->type + DNGN_START_OF_MONSTERS;
            env.show_col[ex][ey] = monster->colour;

            if (mons_friendly(monster))
            {
                env.show_col[ex][ey] |= COLFLAG_FRIENDLY_MONSTER;
            }
            else if (Options.stab_brand != CHATTR_NORMAL
                    && mons_looks_stabbable(monster))
            {
                env.show_col[ex][ey] |= COLFLAG_WILLSTAB;
            }
            else if (Options.may_stab_brand != CHATTR_NORMAL
                    && mons_looks_distracted(monster))
            {
                env.show_col[ex][ey] |= COLFLAG_MAYSTAB;
            }
        }                       // end "if (monster->type != -1 && mons_ner)"
    }                           // end "for s"
}                               // end monster_grid()


bool check_awaken(int mons_aw)
{
    int mons_perc = 0;
    struct monsters *monster = &menv[mons_aw];
    const int mon_holy = mons_holiness(monster);

    // berserkers aren't really concerned about stealth
    if (you.berserker)
        return (true);

    // Repel undead is a holy aura, to which evil creatures are sensitive.
    // Note that even though demons aren't affected by repel undead, they
    // do sense this type of divine aura. -- bwr
    if (you.duration[DUR_REPEL_UNDEAD] 
        && (mon_holy == MH_UNDEAD || mon_holy == MH_DEMONIC))
    {
        return (true);
    }

    // I assume that creatures who can see invisible are very perceptive
    mons_perc = 10 + (mons_intel(monster->type) * 4) + monster->hit_dice
                   + mons_see_invis(monster) * 5;

    // critters that are wandering still have MHITYOU as their foe are
    // still actively on guard for the player, even if they can't see
    // him.  Give them a large bonus (handle_behaviour() will nuke 'foe'
    // after a while, removing this bonus.
    if (monster->behaviour == BEH_WANDER && monster->foe == MHITYOU)
        mons_perc += 15;

    if (!mons_player_visible(monster))
        mons_perc -= 75;

    if (monster->behaviour == BEH_SLEEP)
    {
        if (mon_holy == MH_NATURAL)
        {
            // monster is "hibernating"... reduce chance of waking
            if (mons_has_ench( monster, ENCH_SLEEP_WARY ))
                mons_perc -= 10;
        }
        else // unnatural creature
        {
            // Unnatural monsters don't actually "sleep", they just 
            // haven't noticed an intruder yet... we'll assume that
            // they're diligently on guard.
            mons_perc += 10;
        }
    }

    // glowing with magical contamination isn't very stealthy
    if (you.magic_contamination > 10)
        mons_perc += you.magic_contamination - 10;

    if (mons_perc < 0)
        mons_perc = 0;

    return (random2(stealth) <= mons_perc);
}                               // end check_awaken()

static void set_show_backup( int ex, int ey )
{
    // Must avoid double setting it.  
    // We want the base terrain/item, not the cloud or monster that replaced it.
    if (!Show_Backup[ex][ey])
        Show_Backup[ex][ey] = env.show[ex][ey];
}

static int get_item_dngn_code(const item_def &item)
{
    switch (item.base_type)
    {
    case OBJ_ORBS:
        return (DNGN_ITEM_ORB);
    case OBJ_WEAPONS:
        return (DNGN_ITEM_WEAPON);
    case OBJ_MISSILES:
        return (DNGN_ITEM_MISSILE);
    case OBJ_ARMOUR:
        return (DNGN_ITEM_ARMOUR);
    case OBJ_WANDS:
        return (DNGN_ITEM_WAND);
    case OBJ_FOOD:
        return (DNGN_ITEM_FOOD);
    case OBJ_SCROLLS:
        return (DNGN_ITEM_SCROLL);
    case OBJ_JEWELLERY:
        return (jewellery_is_amulet(item)? DNGN_ITEM_AMULET : DNGN_ITEM_RING);
    case OBJ_POTIONS:
        return (DNGN_ITEM_POTION);
    case OBJ_BOOKS:
        return (DNGN_ITEM_BOOK);
    case OBJ_STAVES:
        return (DNGN_ITEM_STAVE);
    case OBJ_MISCELLANY:
        return (DNGN_ITEM_MISCELLANY);
    case OBJ_CORPSES:
        return (DNGN_ITEM_CORPSE);
    case OBJ_GOLD:
        return (DNGN_ITEM_GOLD);
    default:
        return (DNGN_ITEM_ORB); // bad item character
   }
}

void item_grid()
{
    char count_x, count_y;

    for (count_y = (you.y_pos - 8); (count_y < you.y_pos + 9); count_y++)
    {
        for (count_x = (you.x_pos - 8); (count_x < you.x_pos + 9); count_x++)
        {
            if (count_x >= 0 && count_x < GXM && count_y >= 0 && count_y < GYM)
            {
                if (igrd[count_x][count_y] != NON_ITEM)
                {
                    const int ix = count_x - you.x_pos + 9;
                    const int iy = count_y - you.y_pos + 9;
                    if (env.show[ix][iy])
                    {
                        const item_def &eitem = mitm[igrd[count_x][count_y]];
                        unsigned short &ecol = env.show_col[ix][iy];
                        
                        ecol = (grd[count_x][count_y] == DNGN_SHALLOW_WATER)?
                                        CYAN
                                      : eitem.colour;

                        if (eitem.link != NON_ITEM)
                        {
                            ecol |= COLFLAG_ITEM_HEAP;
                        }
                        env.show[ix][iy] = get_item_dngn_code( eitem );
                    }
                }
            }
        }                       // end of "for count_y, count_x"
    }
}                               // end item()


void cloud_grid(void)
{
    int mnc = 0;

    // btw, this is also the 'default' color {dlb}
    unsigned char which_colour = LIGHTGREY;

    for (int s = 0; s < MAX_CLOUDS; s++)
    {
        // can anyone explain this??? {dlb}
        // its an optimization to avoid looking past the last cloud -bwr
        if (mnc > env.cloud_no) 
            break;

        if (env.cloud[s].type != CLOUD_NONE)
        {
            mnc++;

            if (see_grid(env.cloud[s].x, env.cloud[s].y))
            {
                const int ex = env.cloud[s].x - you.x_pos + 9;
                const int ey = env.cloud[s].y - you.y_pos + 9;
                
                switch (env.cloud[s].type)
                {
                case CLOUD_FIRE:
                case CLOUD_FIRE_MON:
                    if (env.cloud[s].decay <= 20)
                        which_colour = RED;
                    else if (env.cloud[s].decay <= 40)
                        which_colour = LIGHTRED;
                    else if (one_chance_in(4))
                        which_colour = RED;
                    else if (one_chance_in(4))
                        which_colour = LIGHTRED;
                    else
                        which_colour = YELLOW;
                    break;

                case CLOUD_STINK:
                case CLOUD_STINK_MON:
                    which_colour = GREEN;
                    break;

                case CLOUD_COLD:
                case CLOUD_COLD_MON:
                    if (env.cloud[s].decay <= 20)
                        which_colour = BLUE;
                    else if (env.cloud[s].decay <= 40)
                        which_colour = LIGHTBLUE;
                    else if (one_chance_in(4))
                        which_colour = BLUE;
                    else if (one_chance_in(4))
                        which_colour = LIGHTBLUE;
                    else
                        which_colour = WHITE;
                    break;

                case CLOUD_POISON:
                case CLOUD_POISON_MON:
                    which_colour = (one_chance_in(3) ? LIGHTGREEN : GREEN);
                    break;

                case CLOUD_BLUE_SMOKE:
                case CLOUD_BLUE_SMOKE_MON:
                    which_colour = LIGHTBLUE;
                    break;

                case CLOUD_PURP_SMOKE:
                case CLOUD_PURP_SMOKE_MON:
                    which_colour = MAGENTA;
                    break;

                case CLOUD_MIASMA:
                case CLOUD_MIASMA_MON:
                case CLOUD_BLACK_SMOKE:
                case CLOUD_BLACK_SMOKE_MON:
                    which_colour = DARKGREY;
                    break;

                case CLOUD_MIST:
                    which_colour = EC_MIST;
                    break;

                default:
                    which_colour = LIGHTGREY;
                    break;
                }

                set_show_backup(ex, ey);
                env.show[ex][ey] = DNGN_CLOUD;
                env.show_col[ex][ey] = which_colour;
            }
        }                       // end 'if != CLOUD_NONE'
    }                           // end 'for s' loop
}                               // end cloud_grid()

// Noisy now has a messenging service for giving messages to the 
// player is appropriate.
//
// Returns true if the PC heard the noise.
bool noisy( int loudness, int nois_x, int nois_y, const char *msg )
{
    int p;
    struct monsters *monster = 0;       // NULL {dlb}
    bool ret = false;

    // If the origin is silenced there is no noise.
    if (silenced( nois_x, nois_y ))
        return (false);

    const int dist = loudness * loudness;

    // message the player
    if (distance( you.x_pos, you.y_pos, nois_x, nois_y ) <= dist
        && player_can_hear( nois_x, nois_y ))
    {
        if (msg)
            mpr( msg, MSGCH_SOUND );

        ret = true;
    }
    
    for (p = 0; p < MAX_MONSTERS; p++)
    {
        monster = &menv[p];

        if (monster->type < 0)
            continue;

        if (distance(monster->x, monster->y, nois_x, nois_y) <= dist
            && !silenced(monster->x, monster->y))
        {
            // If the noise came from the character, any nearby monster
            // will be jumping on top of them.
            if (nois_x == you.x_pos && nois_y == you.y_pos)
                behaviour_event( monster, ME_ALERT, MHITYOU );
            else
                behaviour_event( monster, ME_DISTURB, MHITNOT, nois_x, nois_y );
        }
    }

    return (ret);
}                               // end noisy()

/* The LOS code now uses raycasting -- haranp */

#define LONGSIZE (sizeof(unsigned long)*8)
#define LOS_MAX_RANGE_X 9
#define LOS_MAX_RANGE_Y 9
#define LOS_MAX_RANGE 9

// the following two constants represent the 'middle' of the sh array.
// since the current shown area is 19x19, centering the view at (9,9)
// means it will be exactly centered.
// This is done to accomodate possible future changes in viewable screen
// area - simply change sh_xo and sh_yo to the new view center.

const int sh_xo = 9;            // X and Y origins for the sh array
const int sh_yo = 9;

// Data used for the LOS algorithm
int los_radius_squared = 8*8 + 1;

unsigned long* los_blockrays = NULL;
unsigned long* dead_rays = NULL;
std::vector<short> ray_coord_x;
std::vector<short> ray_coord_y;
std::vector<short> compressed_ray_x;
std::vector<short> compressed_ray_y;
std::vector<int> raylengths;
std::vector<ray_def> fullrays;

void setLOSRadius(int newLR)
{
    los_radius_squared = newLR * newLR + 1*1;
}

bool get_bit_in_long_array( const unsigned long* data, int where )
{
    int wordloc = where / LONGSIZE;
    int bitloc = where % LONGSIZE;
    return ((data[wordloc] & (1UL << bitloc)) != 0);
}

static void set_bit_in_long_array( unsigned long* data, int where ) {
    int wordloc = where / LONGSIZE;
    int bitloc = where % LONGSIZE;
    data[wordloc] |= (1UL << bitloc);
}

#define EPSILON_VALUE 0.00001
bool double_is_zero( const double x )
{
    return (x > -EPSILON_VALUE) && (x < EPSILON_VALUE);
}

// note that slope must be nonnegative!
// returns 0 if the advance was in x, 1 if it was in y, 2 if it was
// the diagonal
static int find_next_intercept(double* accx, double* accy, const double slope)
{

    // handle perpendiculars
    if ( double_is_zero(slope) )
    {
        *accx += 1.0;
        return 0;
    }
    if ( slope > 100.0 )
    {
        *accy += 1.0;
        return 1;
    }

    const double xtarget = (double)((int)(*accx) + 1);
    const double ytarget = (double)((int)(*accy) + 1);
    const double xdistance = xtarget - *accx;
    const double ydistance = ytarget - *accy;
    const double distdiff = (xdistance * slope - ydistance);

    // exact corner
    if ( double_is_zero( distdiff ) ) {
        // move somewhat away from the corner
        if ( slope > 1.0 ) {
            *accx = xtarget + EPSILON_VALUE * 2;
            *accy = ytarget + EPSILON_VALUE * 2 * slope;
        }
        else {
            *accx = xtarget + EPSILON_VALUE * 2 / slope;
            *accy = ytarget + EPSILON_VALUE * 2;
        }
        return 2;
    }

    double traveldist;
    int rc = -1;
    if ( distdiff > 0.0 ) {
        traveldist = ydistance / slope;
        rc = 1;
    }
    else {
        traveldist = xdistance;
        rc = 0;
    }

    traveldist += EPSILON_VALUE * 10.0;

    *accx += traveldist;
    *accy += traveldist * slope;
    return rc;
}

void ray_def::advance_and_bounce()
{
    // 0 = down-right, 1 = down-left, 2 = up-left, 3 = up-right
    int bouncequad[4][3] = {
        { 1, 3, 2 }, { 0, 2, 3 }, { 3, 1, 0 }, { 2, 0, 1 }
    };
    int oldx = x(), oldy = y();
    int rc = advance();
    int newx = x(), newy = y();
    ASSERT( grid_is_solid(grd[newx][newy]) );
    if ( double_is_zero(slope) || slope > 100.0 )
        quadrant = bouncequad[quadrant][2];
    else if ( rc != 2 )
        quadrant = bouncequad[quadrant][rc];
    else
    {
        ASSERT( (oldx != newx) && (oldy != newy) );
        bool blocked_x = grid_is_solid(grd[oldx][newy]);
        bool blocked_y = grid_is_solid(grd[newx][oldy]);
        if ( blocked_x && blocked_y )
            quadrant = bouncequad[quadrant][rc];
        else if ( blocked_x )
            quadrant = bouncequad[quadrant][1];
        else
            quadrant = bouncequad[quadrant][0];
    }
    advance();
}

void ray_def::regress()
{
    int opp_quadrant[4] = { 2, 3, 0, 1 };
    quadrant = opp_quadrant[quadrant];
    advance();
    quadrant = opp_quadrant[quadrant];
}

int ray_def::advance()
{
    int rc;
    switch ( quadrant )
    {
    case 0:
        // going down-right
        rc = find_next_intercept( &accx, &accy, slope );
        return rc;
    case 1:
        // going down-left
        accx = 100.0 - EPSILON_VALUE/10.0 - accx;
        rc = find_next_intercept( &accx, &accy, slope );
        accx = 100.0 - EPSILON_VALUE/10.0 - accx;
        return rc;
    case 2:
        // going up-left
        accx = 100.0 - EPSILON_VALUE/10.0 - accx;
        accy = 100.0 - EPSILON_VALUE/10.0 - accy;
        rc = find_next_intercept( &accx, &accy, slope );
        accx = 100.0 - EPSILON_VALUE/10.0 - accx;
        accy = 100.0 - EPSILON_VALUE/10.0 - accy;        
        return rc;
    case 3:
        // going up-right
        accy = 100.0 - EPSILON_VALUE/10.0 - accy;
        rc = find_next_intercept( &accx, &accy, slope );
        accy = 100.0 - EPSILON_VALUE/10.0 - accy;        
        return rc;
    default:
        return -1;
    }
}

// Shoot a ray from the given start point (accx, accy) with the given
// slope, with a maximum distance (in either x or y coordinate) of
// maxrange. Store the visited cells in xpos[] and ypos[], and
// return the number of cells visited.
static int shoot_ray( double accx, double accy, const double slope,
                      int maxrange, int xpos[], int ypos[] )
{
    int curx, cury;
    int cellnum;
    for ( cellnum = 0; true; ++cellnum )
    {
        find_next_intercept( &accx, &accy, slope );
        curx = (int)(accx);
        cury = (int)(accy);
        if ( curx > maxrange || cury > maxrange )
            break;

        // work with the new square
        xpos[cellnum] = curx;
        ypos[cellnum] = cury;
    }
    return cellnum;
}

// check if the passed ray has already been created
static bool is_duplicate_ray( int len, int xpos[], int ypos[] )
{
    int cur_offset = 0;
    for ( unsigned int i = 0; i < raylengths.size(); ++i )
    {
        // only compare equal-length rays
        if ( raylengths[i] != len )
        {
            cur_offset += raylengths[i];
            continue;
        }

        int j;
        for ( j = 0; j < len; ++j )
        {
            if ( ray_coord_x[j + cur_offset] != xpos[j] ||
                 ray_coord_y[j + cur_offset] != ypos[j] )
                break;
        }

        // exact duplicate?
        if ( j == len )
            return true;

        // move to beginning of next ray
        cur_offset += raylengths[i];
    }
    return false;
}

// is starta...lengtha a subset of startb...lengthb?
static bool is_subset( int starta, int startb, int lengtha, int lengthb )
{
    int cura = starta, curb = startb;
    int enda = starta + lengtha, endb = startb + lengthb;
    while ( cura < enda && curb < endb )
    {
        if ( ray_coord_x[curb] > ray_coord_x[cura] )
            return false;
        if ( ray_coord_y[curb] > ray_coord_y[cura] )
            return false;
        if ( ray_coord_x[cura] == ray_coord_x[curb] &&
             ray_coord_y[cura] == ray_coord_y[curb] )
            ++cura;

        ++curb;
    }
    return ( cura == enda );
}

// return a vector which lists all the nonduped cellrays (by index)
static std::vector<int> find_nonduped_cellrays()
{
    // a cellray c in a fullray f is duped if there is a fullray g
    // such that g contains c and g[:c] is a subset of f[:c]
    int raynum, cellnum, curidx, testidx, testray, testcell;
    bool is_duplicate;

    std::vector<int> result;
    for (curidx=0, raynum=0;
         raynum < (int)raylengths.size();
         curidx += raylengths[raynum++])
    {
        for (cellnum = 0; cellnum < raylengths[raynum]; ++cellnum)
        {
            // is the cellray raynum[cellnum] duplicated?
            is_duplicate = false;
            // XXX We should really check everything up to now
            // completely, and all further rays to see if they're
            // proper subsets.
            const int curx = ray_coord_x[curidx + cellnum];
            const int cury = ray_coord_y[curidx + cellnum];
            for (testidx = 0, testray = 0; testray < raynum;
                 testidx += raylengths[testray++])
            {
                // scan ahead to see if there's an intersect
                for ( testcell = 0; testcell < raylengths[raynum]; ++testcell )
                {
                    const int testx = ray_coord_x[testidx + testcell];
                    const int testy = ray_coord_y[testidx + testcell];
                    // we can short-circuit sometimes
                    if ( testx > curx || testy > cury )
                        break;
                    // bingo!
                    if ( testx == curx && testy == cury )
                    {
                        is_duplicate = is_subset(testidx, curidx,
                                                 testcell, cellnum);
                        break;
                    }
                }
                if ( is_duplicate )
                    break;      // no point in checking further rays
            }
            if ( !is_duplicate )
                result.push_back(curidx + cellnum);
        }
    }
    return result;
}

// Create and register the ray defined by the arguments.
// Return true if the ray was actually registered (i.e., not a duplicate.)
static bool register_ray( double accx, double accy, double slope )
{
    int xpos[LOS_MAX_RANGE * 2 + 1], ypos[LOS_MAX_RANGE * 2 + 1];
    int raylen = shoot_ray( accx, accy, slope, LOS_MAX_RANGE, xpos, ypos );

    // early out if ray already exists
    if ( is_duplicate_ray(raylen, xpos, ypos) )
        return false;

    // not duplicate, register
    for ( int i = 0; i < raylen; ++i )
    {
        // create the cellrays
        ray_coord_x.push_back(xpos[i]);
        ray_coord_y.push_back(ypos[i]);
    }

    // register the fullray
    raylengths.push_back(raylen);
    ray_def ray;
    ray.accx = accx;
    ray.accy = accy;
    ray.slope = slope;
    ray.quadrant = 0;
    fullrays.push_back(ray);

    return true;
}

static void create_blockrays()
{
    // determine nonduplicated rays
    std::vector<int> nondupe_cellrays = find_nonduped_cellrays();
    const unsigned int num_nondupe_rays = nondupe_cellrays.size();
    const unsigned int num_nondupe_words =
        (num_nondupe_rays + LONGSIZE - 1) / LONGSIZE;
    const unsigned int num_cellrays = ray_coord_x.size();
    const unsigned int num_words = (num_cellrays + LONGSIZE - 1) / LONGSIZE;

    // first build all the rays: easier to do blocking calculations there
    unsigned long* full_los_blockrays;
    full_los_blockrays = new unsigned long[num_words * (LOS_MAX_RANGE_X+1) *
                                           (LOS_MAX_RANGE_Y+1)];
    memset((void*)full_los_blockrays, 0, sizeof(unsigned long) * num_words *
           (LOS_MAX_RANGE_X+1) * (LOS_MAX_RANGE_Y+1));

    int cur_offset = 0;

    for ( unsigned int ray = 0; ray < raylengths.size(); ++ray )
    {
        for ( int i = 0; i < raylengths[ray]; ++i )
        {
            // every cell blocks...
            unsigned long* const inptr = full_los_blockrays +
                (ray_coord_x[i + cur_offset] * (LOS_MAX_RANGE_Y + 1) +
                 ray_coord_y[i + cur_offset]) * num_words;

            // ...all following cellrays
            for ( int j = i+1; j < raylengths[ray]; ++j )
                set_bit_in_long_array( inptr, j + cur_offset );

        }
        cur_offset += raylengths[ray];
    }

    // we've built the basic blockray array; now compress it, keeping
    // only the nonduplicated cellrays.

    // allocate and clear memory
    los_blockrays = new unsigned long[num_nondupe_words * (LOS_MAX_RANGE_X+1) * (LOS_MAX_RANGE_Y + 1)];
    memset((void*)los_blockrays, 0, sizeof(unsigned long) * num_nondupe_words *
           (LOS_MAX_RANGE_X+1) * (LOS_MAX_RANGE_Y+1));

    // we want to only keep the cellrays from nondupe_cellrays.
    compressed_ray_x.resize(num_nondupe_rays);
    compressed_ray_y.resize(num_nondupe_rays);
    for ( unsigned int i = 0; i < num_nondupe_rays; ++i )
    {
        compressed_ray_x[i] = ray_coord_x[nondupe_cellrays[i]];
        compressed_ray_y[i] = ray_coord_y[nondupe_cellrays[i]];
    }
    unsigned long* oldptr = full_los_blockrays;
    unsigned long* newptr = los_blockrays;
    for ( int x = 0; x <= LOS_MAX_RANGE_X; ++x )
    {
        for ( int y = 0; y <= LOS_MAX_RANGE_Y; ++y )
        {
            for ( unsigned int i = 0; i < num_nondupe_rays; ++i )
                if ( get_bit_in_long_array(oldptr, nondupe_cellrays[i]) )
                    set_bit_in_long_array(newptr, i);
            oldptr += num_words;
            newptr += num_nondupe_words;
        }
    }

    // we can throw away full_los_blockrays now
    delete [] full_los_blockrays;

    dead_rays = new unsigned long[num_nondupe_words];
    
#ifdef DEBUG_DIAGNOSTICS
    mprf( MSGCH_DIAGNOSTICS, "Cellrays: %d Fullrays: %u Compressed: %u",
          num_cellrays, raylengths.size(), num_nondupe_rays );
#endif
}

static int gcd( int x, int y )
{
    int tmp;
    while ( y != 0 )
    {
        x %= y;
        tmp = x;
        x = y;
        y = tmp;
    }
    return x;
}

// Cast all rays
void raycast()
{
    static bool done_raycast = false;
    if ( done_raycast )
        return;
    
    // Creating all rays for first quadrant
    // We have a considerable amount of overkill.   
    done_raycast = true;

    int xangle, yangle;

    // register perpendiculars FIRST, to make them top choice
    // when selecting beams
    register_ray( 0.5, 0.5, 1000.0 );
    register_ray( 0.5, 0.5, 0.0 );

    // For a slope of M = y/x, every x we move on the X axis means
    // that we move y on the y axis. We want to look at the resolution
    // of x/y: in that case, every step on the X axis means an increase
    // of 1 in the Y axis at the intercept point. We can assume gcd(x,y)=1,
    // so we look at steps of 1/y.
    for ( xangle = 1; xangle <= LOS_MAX_RANGE; ++xangle ) {
        for ( yangle = 1; yangle <= LOS_MAX_RANGE; ++yangle ) {

            if ( gcd(xangle, yangle) != 1 )
                continue;

            const double slope = ((double)(yangle)) / xangle;
            const double rslope = ((double)(xangle)) / yangle;
            for ( int intercept = 0; intercept <= yangle; ++intercept ) {
                double xstart = ((double)(intercept)) / yangle;
                if ( intercept == 0 )
                    xstart += EPSILON_VALUE / 10.0;
                if ( intercept == yangle )
                    xstart -= EPSILON_VALUE / 10.0;
                // y should be "about to change"            
                register_ray( xstart, 1.0 - EPSILON_VALUE / 10.0, slope );
                // also draw the identical ray in octant 2
                register_ray( 1.0 - EPSILON_VALUE / 10.0, xstart, rslope );
            }
        }
    }

    // Now create the appropriate blockrays array
    create_blockrays();   
}

static void set_ray_quadrant( ray_def& ray, int sx, int sy, int tx, int ty )
{
    if ( tx >= sx && ty >= sy )
        ray.quadrant = 0;
    else if ( tx < sx && ty >= sy )
        ray.quadrant = 1;
    else if ( tx < sx && ty < sy )
        ray.quadrant = 2;
    else if ( tx >= sx && ty < sy )
        ray.quadrant = 3;
    else
        mpr("Bad ray quadrant!", MSGCH_DIAGNOSTICS);
}


// Find a nonblocked ray from sx, sy to tx, ty. Return false if no
// such ray could be found, otherwise return true and fill ray
// appropriately.
// If allow_fallback is true, fall back to a center-to-center ray
// if range is too great or all rays are blocked.
bool find_ray( int sourcex, int sourcey, int targetx, int targety,
               bool allow_fallback, ray_def& ray )
{
    
    int cellray, inray;
    const int signx = ((targetx - sourcex >= 0) ? 1 : -1);
    const int signy = ((targety - sourcey >= 0) ? 1 : -1);
    const int absx = signx * (targetx - sourcex);
    const int absy = signy * (targety - sourcey);
    int cur_offset = 0;
    for ( unsigned int fullray = 0; fullray < fullrays.size();
          cur_offset += raylengths[fullray++] ) {

        for ( cellray = 0; cellray < raylengths[fullray]; ++cellray )
        {
            if ( ray_coord_x[cellray + cur_offset] == absx &&
                 ray_coord_y[cellray + cur_offset] == absy ) {

                // check if we're blocked so far
                bool blocked = false;
                for ( inray = 0; inray < cellray; ++inray ) {
                    if (grid_is_solid(grd[sourcex + signx * ray_coord_x[inray + cur_offset]][sourcey + signy * ray_coord_y[inray + cur_offset]]))
                    {
                        blocked = true;                   
                        break;
                    }
                }

                if ( !blocked )
                {
                    // success!
                    ray = fullrays[fullray];
                    if ( sourcex > targetx )
                        ray.accx = 1.0 - ray.accx;
                    if ( sourcey > targety )
                        ray.accy = 1.0 - ray.accy;
                    ray.accx += sourcex;
                    ray.accy += sourcey;
                    set_ray_quadrant(ray, sourcex, sourcey, targetx, targety);
                    return true;
                }
            }
        }
    }
    if ( allow_fallback ) {
        ray.accx = sourcex + 0.5;
        ray.accy = sourcey + 0.5;
        if ( targetx == sourcex )
            ray.slope = 10000.0;
        else {
            ray.slope = targety - sourcey;
            ray.slope /= targetx - sourcex;
            if ( ray.slope < 0 )
                ray.slope = -ray.slope;
        }
        set_ray_quadrant(ray, sourcex, sourcey, targetx, targety);
        return true;
    }
    return false;
}

// The rule behind LOS is:
// Two cells can see each other if there is any line from some point
// of the first to some point of the second ("generous" LOS.)
//
// We use raycasting. The algorithm:
// PRECOMPUTATION:
// Create a large bundle of rays and cast them.
// Mark, for each one, which cells kill it (and where.)
// Also, for each one, note which cells it passes.
// ACTUAL LOS:
// Unite the ray-killers for the given map; this tells you which rays
// are dead.
// Look up which cells the surviving rays have, and that's your LOS!
// OPTIMIZATIONS:
// WLOG, we can assume that we're in a specific quadrant - say the
// first quadrant - and just mirror everything after that.  We can
// likely get away with a single octant, but we don't do that. (To
// do...)
// Rays are actually split by each cell they pass. So each "ray" only
// identifies a single cell, and we can do logical ORs.  Once a cell
// kills a cellray, it will kill all remaining cellrays of that ray.
// Also, rays are checked to see if they are duplicates of each
// other. If they are, they're eliminated.
// Some cellrays can also be eliminated. In general, a cellray is
// unnecessary if there is another cellray with the same coordinates,
// and whose path (up to those coordinates) is a subset, not necessarily
// proper, of the original path. We still store the original cellrays
// fully for beam detection and such.
// PERFORMANCE:
// With reasonable values we have around 6000 cellrays, meaning
// around 600Kb (75 KB) of data. This gets cut down to 700 cellrays
// after removing duplicates. That means that we need to do
// around 22*100*4 ~ 9,000 memory reads + writes per LOS call on a
// 32-bit system. Not too bad.
void losight(FixedArray < unsigned int, 19, 19 > &sh,
             FixedArray < unsigned char, 80, 70 > &gr, int x_p, int y_p)
{
    raycast();
    // go quadrant by quadrant
    int quadrant_x[4] = {  1, -1, -1,  1 };
    int quadrant_y[4] = {  1,  1, -1, -1 };

    // clear out sh
    for ( int i = 0; i < 19; ++i )
        for ( int j = 0; j < 19; ++j )
            sh[i][j] = 0;

    const unsigned int num_cellrays = compressed_ray_x.size();
    const unsigned int num_words = (num_cellrays + LONGSIZE - 1) / LONGSIZE;

    for ( int quadrant = 0; quadrant < 4; ++quadrant ) {
        const int xmult = quadrant_x[quadrant];
        const int ymult = quadrant_y[quadrant];

        // clear out the dead rays array
        memset( (void*)dead_rays, 0, sizeof(unsigned long) * num_words);

        // kill all blocked rays
        const unsigned long* inptr = los_blockrays;
        for ( int xdiff = 0; xdiff <= LOS_MAX_RANGE_X; ++xdiff ) {
            for (int ydiff = 0; ydiff <= LOS_MAX_RANGE_Y;
                 ++ydiff, inptr += num_words ) {
                
                const int realx = x_p + xdiff * xmult;
                const int realy = y_p + ydiff * ymult;

                if (realx < 0 || realx > 79 || realy < 0 || realy > 69)
                    continue;

                // if this cell is opaque...
                if ( grid_is_opaque(gr[realx][realy])) {
                    // then block the appropriate rays
                    for ( unsigned int i = 0; i < num_words; ++i )
                        dead_rays[i] |= inptr[i];
                }
            }
        }

        // ray calculation done, now work out which cells in this
        // quadrant are visible
        unsigned int rayidx = 0;
        for ( unsigned int wordloc = 0; wordloc < num_words; ++wordloc ) {
            const unsigned long curword = dead_rays[wordloc];
            // Note: the last word may be incomplete
            for ( unsigned int bitloc = 0; bitloc < LONGSIZE; ++bitloc) {
                // make the cells seen by this ray at this point visible
                if ( ((curword >> bitloc) & 1UL) == 0 ) {
                    // this ray is alive!
                    const int realx = xmult * compressed_ray_x[rayidx];
                    const int realy = ymult * compressed_ray_y[rayidx];
                    // update shadow map
                    if (x_p + realx >= 0 && x_p + realx < 80 &&
                        y_p + realy >= 0 && y_p + realy < 70 &&
                        realx * realx + realy * realy <= los_radius_squared )
                        sh[sh_xo+realx][sh_yo+realy]=gr[x_p+realx][y_p+realy];
                }
                ++rayidx;
                if ( rayidx == num_cellrays )
                    break;
            }
        }
    }

    // [dshaligram] The player's current position is always visible.
    sh[sh_xo][sh_yo] = gr[x_p][y_p];
}


void draw_border(void)
{
    textcolor( BORDER_COLOR );
    clrscr();
    redraw_skill( you.your_name, player_title() );

    gotoxy(40, 2);
    cprintf( "%s %s", species_name( you.species, you.experience_level ), 
                     (you.wizard ? "*WIZARD*" : "" ) );

    gotoxy(40,  3); cprintf("HP:");
    gotoxy(40,  4); cprintf("Magic:");
    gotoxy(40,  5); cprintf("AC:");
    gotoxy(40,  6); cprintf("EV:");
    gotoxy(40,  7); cprintf("Str:");
    gotoxy(40,  8); cprintf("Int:");
    gotoxy(40,  9); cprintf("Dex:");
    gotoxy(40, 10); cprintf("Gold:");
    gotoxy(55, 10); cprintf("Turn:");
    gotoxy(40, 11); cprintf("Experience:");
    gotoxy(40, 12); cprintf("Level");
}                               // end draw_border()

// Determines if the given feature is present at (x, y) in _grid_ coordinates.
// If you have map coords, add (1, 1) to get grid coords.
// Use one of
// 1. '<' and '>' to look for stairs
// 2. '\t' or '\\' for shops, portals.
// 3. '^' for traps
// 4. '_' for altars
// 5. Anything else will look for the exact same character in the level map.
bool is_feature(int feature, int x, int y) {
    unsigned char envfeat = (unsigned char) env.map[x - 1][y - 1];
    if (!envfeat)
        return false;

    // 'grid' can fit in an unsigned char, but making this a short shuts up
    // warnings about out-of-range case values.
    short grid = grd[x][y];

    switch (feature) {
    case 'X':
        return (point_distance[x][y] == PD_EXCLUDED);
    case 'F':
    case 'W':
        return is_waypoint(x, y);
    case 'I':
        return is_stash(x, y);
    case '_':
        switch (grid) {
        case DNGN_ALTAR_ZIN:
        case DNGN_ALTAR_SHINING_ONE:
        case DNGN_ALTAR_KIKUBAAQUDGHA:
        case DNGN_ALTAR_YREDELEMNUL:
        case DNGN_ALTAR_XOM:
        case DNGN_ALTAR_VEHUMET:
        case DNGN_ALTAR_OKAWARU:
        case DNGN_ALTAR_MAKHLEB:
        case DNGN_ALTAR_SIF_MUNA:
        case DNGN_ALTAR_TROG:
        case DNGN_ALTAR_NEMELEX_XOBEH:
        case DNGN_ALTAR_ELYVILON:
        case DNGN_ALTAR_LUCY:
            return true;
        default:
            return false;
        }
    case '\t':
    case '\\':
        switch (grid) {
        case DNGN_ENTER_HELL:
        case DNGN_ENTER_LABYRINTH:
        case DNGN_ENTER_SHOP:
        case DNGN_ENTER_DIS:
        case DNGN_ENTER_GEHENNA:
        case DNGN_ENTER_COCYTUS:
        case DNGN_ENTER_TARTARUS:
        case DNGN_ENTER_ABYSS:
        case DNGN_EXIT_ABYSS:
        case DNGN_STONE_ARCH:
        case DNGN_ENTER_PANDEMONIUM:
        case DNGN_EXIT_PANDEMONIUM:
        case DNGN_TRANSIT_PANDEMONIUM:
        case DNGN_ENTER_ZOT:
        case DNGN_RETURN_FROM_ZOT:
            return true;
        default:
            return false;
        }
    case '<':
        switch (grid) {
        case DNGN_ROCK_STAIRS_UP:
        case DNGN_STONE_STAIRS_UP_I:
        case DNGN_STONE_STAIRS_UP_II:
        case DNGN_STONE_STAIRS_UP_III:
        case DNGN_RETURN_FROM_ORCISH_MINES:
        case DNGN_RETURN_FROM_HIVE:
        case DNGN_RETURN_FROM_LAIR:
        case DNGN_RETURN_FROM_SLIME_PITS:
        case DNGN_RETURN_FROM_VAULTS:
        case DNGN_RETURN_FROM_CRYPT:
        case DNGN_RETURN_FROM_HALL_OF_BLADES:
        case DNGN_RETURN_FROM_TEMPLE:
        case DNGN_RETURN_FROM_SNAKE_PIT:
        case DNGN_RETURN_FROM_ELVEN_HALLS:
        case DNGN_RETURN_FROM_TOMB:
        case DNGN_RETURN_FROM_SWAMP:
            return true;
        default:
            return false;
        }
    case '>':
        switch (grid) {
        case DNGN_ROCK_STAIRS_DOWN:
        case DNGN_STONE_STAIRS_DOWN_I:
        case DNGN_STONE_STAIRS_DOWN_II:
        case DNGN_STONE_STAIRS_DOWN_III:
        case DNGN_ENTER_ORCISH_MINES:
        case DNGN_ENTER_HIVE:
        case DNGN_ENTER_LAIR:
        case DNGN_ENTER_SLIME_PITS:
        case DNGN_ENTER_VAULTS:
        case DNGN_ENTER_CRYPT:
        case DNGN_ENTER_HALL_OF_BLADES:
        case DNGN_ENTER_TEMPLE:
        case DNGN_ENTER_SNAKE_PIT:
        case DNGN_ENTER_ELVEN_HALLS:
        case DNGN_ENTER_TOMB:
        case DNGN_ENTER_SWAMP:
            return true;
        default:
            return false;
        }
    case '^':
        switch (grid) {
        case DNGN_TRAP_MECHANICAL:
        case DNGN_TRAP_MAGICAL:
        case DNGN_TRAP_III:
            return true;
        default:
            return false;
        }
    default: 
        return envfeat == feature;
    }
}

static int find_feature(unsigned char feature, int curs_x, int curs_y, 
                         int start_x, int start_y, int anchor_x, int anchor_y,
                         int ignore_count, char *move_x, char *move_y) {
    int cx = anchor_x,
        cy = anchor_y;

    int firstx = -1, firsty = -1;
    int matchcount = 0;

    // Find the first occurrence of feature 'feature', spiralling around (x,y)
    int maxradius = GXM > GYM? GXM : GYM;
    for (int radius = 1; radius < maxradius; ++radius) {
        for (int axis = -2; axis < 2; ++axis) {
            int rad = radius - (axis < 0);
            for (int var = -rad; var <= rad; ++var) {
                int dx = radius, dy = var;
                if (axis % 2)
                    dx = -dx;
                if (axis < 0) {
                    int temp = dx;
                    dx = dy;
                    dy = temp;
                }

                int x = cx + dx, y = cy + dy;
                if (x < 0 || y < 0 || x >= GXM || y >= GYM) continue;
                if (is_feature(feature, x + 1, y + 1)) {
                    ++matchcount;
                    if (!ignore_count--) {
                        // We want to cursor to (x,y)
                        *move_x = x - (start_x + curs_x - 1);
                        *move_y = y - (start_y + curs_y - 1);
                        return matchcount;
                    }
                    else if (firstx == -1) {
                        firstx = x;
                        firsty = y;
                    }
                }
            }
        }
    }

    // We found something, but ignored it because of an ignorecount
    if (firstx != -1) {
        *move_x = firstx - (start_x + curs_x - 1);
        *move_y = firsty - (start_y + curs_y - 1);
        return 1;
    }
    return 0;
}

void find_features(const std::vector<coord_def>& features,
        unsigned char feature, std::vector<coord_def> *found) {
    for (unsigned feat = 0; feat < features.size(); ++feat) {
        const coord_def& coord = features[feat];
        if (is_feature(feature, coord.x, coord.y))
            found->push_back(coord);
    }
}

static int find_feature( const std::vector<coord_def>& features,
                         unsigned char feature, int curs_x, int curs_y, 
                         int start_x, int start_y, 
                         int ignore_count, 
                         char *move_x, char *move_y,
                         bool forward) {
    int firstx = -1, firsty = -1, firstmatch = -1;
    int matchcount = 0;

    for (unsigned feat = 0; feat < features.size(); ++feat) {
        const coord_def& coord = features[feat];

        if (is_feature(feature, coord.x, coord.y)) {
            ++matchcount;
            if (forward? !ignore_count-- : --ignore_count == 1) {
                // We want to cursor to (x,y)
                *move_x = coord.x - (start_x + curs_x);
                *move_y = coord.y - (start_y + curs_y);
                return matchcount;
            }
            else if (!forward || firstx == -1) {
                firstx = coord.x;
                firsty = coord.y;
                firstmatch = matchcount;
            }
        }
    }

    // We found something, but ignored it because of an ignorecount
    if (firstx != -1) {
        *move_x = firstx - (start_x + curs_x);
        *move_y = firsty - (start_y + curs_y);
        return firstmatch;
    }
    return 0;
}

#ifdef USE_CURSES
// NOTE: This affects libunix.cc draw state; use this just before setting
// textcolour and drawing a character and call set_altcharset(false)
// after you're done drawing.
//
static int cset_adjust(int raw)
{
    if (Options.char_set != CSET_ASCII) 
    {
        // switch to alternate char set for 8-bit characters:
        set_altcharset( raw > 127 );

        // shift the DEC line drawing set:
        if (Options.char_set == CSET_DEC 
            && raw >= 0xE0)
        {
            raw &= 0x7F;
        }
    }
    return (raw);
}
#endif

static void draw_level_map(
        int start_x, int start_y, int screen_y, bool travel_mode)
{
    int bufcount2 = 0;
    screen_buffer_t buffer2[GYM * GXM * 2];        
    const int num_lines = get_number_of_lines();

    cursor_control cs(false);

#ifdef PLAIN_TERM
    gotoxy(1, 1);
#endif

    for (int j = 0; j < num_lines; j++)
    {
        for (int i = 0; i < 80; i++)
        {
            screen_buffer_t colour = DARKGREY;
            if (start_y + j >= 65 || start_y + j <= 3 
                || start_x + i < 0 || start_x + i >= GXM - 1)
            {
                buffer2[bufcount2 + 1] = DARKGREY;
                buffer2[bufcount2] = 0;
                bufcount2 += 2;

#ifdef PLAIN_TERM
                goto print_it;
#endif

#ifdef DOS_TERM
                continue;
#endif
            }

            colour = colour_code_map(start_x + i, start_y + j,
                            Options.item_colour, 
                            travel_mode && Options.travel_colour);

            buffer2[bufcount2 + 1] = colour;
            buffer2[bufcount2] = 
                        (unsigned char) env.map[start_x + i][start_y + j];
            
            if (start_x + i + 1 == you.x_pos && start_y + j + 1 == you.y_pos)
            {
                // [dshaligram] Draw the @ symbol on the level-map. It's no
                // longer saved into the env.map, so we need to draw it 
                // directly.
                buffer2[bufcount2 + 1] = WHITE;
                buffer2[bufcount2]     = you.symbol;
            }

            // If we've a waypoint on the current square, *and* the square is
            // a normal floor square with nothing on it, show the waypoint
            // number.
            if (Options.show_waypoints)
            {
                // XXX: This is a horrible hack.
                screen_buffer_t &bc = buffer2[bufcount2];
                int gridx = start_x + i + 1, gridy = start_y + j + 1;
                unsigned char ch = is_waypoint(gridx, gridy);
                if (ch && (bc == get_sightmap_char(DNGN_FLOOR) ||
                           bc == get_magicmap_char(DNGN_FLOOR)))
                    bc = ch;
            }
            
            bufcount2 += 2;

#ifdef PLAIN_TERM

          print_it:
            // avoid line wrap
            if (i == 79)
                continue;

            // newline
            if (i == 0 && j > 0)
                gotoxy( 1, j + 1 );

            int ch = buffer2[bufcount2 - 2];
#ifdef USE_CURSES
            ch = cset_adjust( ch );
#endif
            textcolor( buffer2[bufcount2 - 1] );
            putch(ch);
#endif
        }
    }

#ifdef USE_CURSES
    set_altcharset(false);
#endif

#ifdef DOS_TERM
    puttext(1, 1, 80, 25, buffer2);
#endif
}

// show_map() now centers the known map along x or y.  This prevents
// the player from getting "artificial" location clues by using the
// map to see how close to the end they are.  They'll need to explore
// to get that.  This function is still a mess, though. -- bwr
void show_map( FixedVector<int, 2> &spec_place, bool travel_mode )
{
    cursor_control con(true);
    int i, j;

    char move_x = 0;
    char move_y = 0;
    char getty = 0;

#ifdef DOS_TERM
    char buffer[4800];
#endif

    // Vector to track all features we can travel to, in order of distance.
    std::vector<coord_def> features;
    if (travel_mode)
    {
        travel_cache.update();

        find_travel_pos(you.x_pos, you.y_pos, NULL, NULL, &features);
        // Sort features into the order the player is likely to prefer.
        arrange_features(features);
    }

    char min_x = 80, max_x = 0, min_y = 0, max_y = 0;
    bool found_y = false;

    const int num_lines = get_number_of_lines();
    const int half_screen = num_lines / 2 - 1;

    for (j = 0; j < GYM; j++)
    {
        for (i = 0; i < GXM; i++)
        {
            if (env.map[i][j])
            {
                if (!found_y)
                {
                    found_y = true;
                    min_y = j;
                }

                max_y = j;

                if (i < min_x)
                    min_x = i;

                if (i > max_x)
                    max_x = i;
            }
        }
    }

    const int map_lines = max_y - min_y + 1;

    const int start_x = min_x + (max_x - min_x + 1) / 2 - 40; // no x scrolling
    const int block_step = Options.level_map_cursor_step;
    int start_y = 0;                                          // y does scroll

    int screen_y = you.y_pos;

    // if close to top of known map, put min_y on top
    // else if close to bottom of known map, put max_y on bottom.
    //
    // The num_lines comparisons are done to keep things neat, by
    // keeping things at the top of the screen.  By shifting an
    // additional one in the num_lines > map_lines case, we can 
    // keep the top line clear... which makes things look a whole
    // lot better for small maps.
    if (num_lines > map_lines)
        screen_y = min_y + half_screen - 1;
    else if (num_lines == map_lines || screen_y - half_screen < min_y)
        screen_y = min_y + half_screen;
    else if (screen_y + half_screen > max_y)
        screen_y = max_y - half_screen;

    int curs_x = you.x_pos - start_x;
    int curs_y = you.y_pos - screen_y + half_screen;
    int search_feat = 0, search_found = 0, anchor_x = -1, anchor_y = -1;

    bool map_alive  = true;
    bool redraw_map = true;

#ifdef DOS_TERM
    gettext(1, 1, 80, 25, buffer);
    window(1, 1, 80, 25);
#endif

    clrscr();
    textcolor(DARKGREY);

    while (map_alive)
    {
        start_y = screen_y - half_screen;

        if (redraw_map)
            draw_level_map(start_x, start_y, screen_y, travel_mode);

        redraw_map = true;
        gotoxy(curs_x, curs_y);

        getty = unmangle_direction_keys(getchm(KC_LEVELMAP), KC_LEVELMAP);

        switch (getty)
        {
        case '?':
            show_levelmap_help();
            break;

        case CONTROL('C'):
            clear_map();
            break;

        case CONTROL('F'):
        case CONTROL('W'):
            travel_cache.add_waypoint(start_x + curs_x, start_y + curs_y);
            // We need to do this all over again so that the user can jump
            // to the waypoint he just created.
            features.clear();
            find_travel_pos(you.x_pos, you.y_pos, NULL, NULL, &features);
            // Sort features into the order the player is likely to prefer.
            arrange_features(features);
            move_x = move_y = 0;
            break;
        case CONTROL('E'):
        case CONTROL('X'):
            {
                int x = start_x + curs_x, y = start_y + curs_y;
                if (getty == CONTROL('X'))
                    toggle_exclude(x, y);
                else
                    clear_excludes();

                // We now need to redo travel colours
                features.clear();
                find_travel_pos(you.x_pos, you.y_pos, NULL, NULL, &features);
                // Sort features into the order the player is likely to prefer.
                arrange_features(features);

                move_x = move_y = 0;
            }
            break;
            
        case 'b':
        case '1':
            move_x = -1;
            move_y = 1;
            break;

        case 'j':
        case '2':
            move_y = 1;
            move_x = 0;
            break;

        case 'u':
        case '9':
            move_x = 1;
            move_y = -1;
            break;

        case 'k':
        case '8':
            move_y = -1;
            move_x = 0;
            break;

        case 'y':
        case '7':
            move_y = -1;
            move_x = -1;
            break;

        case 'h':
        case '4':
            move_x = -1;
            move_y = 0;
            break;

        case 'n':
        case '3':
            move_y = 1;
            move_x = 1;
            break;

        case 'l':
        case '6':
            move_x = 1;
            move_y = 0;
            break;

        case 'B':
            move_x = -block_step;
            move_y = block_step;
            break;

        case 'J':
            move_y = block_step;
            move_x = 0;
            break;

        case 'U':
            move_x = block_step;
            move_y = -block_step;
            break;

        case 'K':
            move_y = -block_step;
            move_x = 0;
            break;

        case 'Y':
            move_y = -block_step;
            move_x = -block_step;
            break;

        case 'H':
            move_x = -block_step;
            move_y = 0;
            break;

        case 'N':
            move_y = block_step;
            move_x = block_step;
            break;

        case 'L':
            move_x = block_step;
            move_y = 0;
            break;

        case '+':
            move_y = 20;
            move_x = 0;
            break;
        case '-':
            move_y = -20;
            move_x = 0;
            break;
        case '<':
        case '>':
        case '@':
        case '\t':
        case '^':
        case '_':
        case 'X':
        case 'F':
        case 'W':
        case 'I':
        case '*':
        case '/':
        case '\'':
        {
            bool forward = true;

            if (getty == '/' || getty == ';')
                forward = false;

            if (getty == '/' || getty == '*' || getty == ';' || getty == '\'')
                getty = 'I';

            move_x = 0;
            move_y = 0;
            if (anchor_x == -1) {
                anchor_x = start_x + curs_x - 1;
                anchor_y = start_y + curs_y - 1;
            }
            if (search_feat != getty) {
                search_feat         = getty;
                search_found        = 0;
            }
            if (travel_mode)
                search_found = find_feature(features, getty, curs_x, curs_y, 
                                            start_x, start_y,
                                            search_found, 
                                            &move_x, &move_y,
                                            forward);
            else
                search_found = find_feature(getty, curs_x, curs_y,
                                            start_x, start_y,
                                            anchor_x, anchor_y,
                                            search_found, &move_x, &move_y);
            break;
        }
        case '.':
        case '\r':
        case 'S':
        case ',':
        case ';':
        {
            int x = start_x + curs_x, y = start_y + curs_y;
            if (travel_mode && x == you.x_pos && y == you.y_pos)
            {
                if (you.travel_x > 0 && you.travel_y > 0) {
                    move_x = you.travel_x - x;
                    move_y = you.travel_y - y;
                }
                break;
            }
            else {
                spec_place[0] = x;
                spec_place[1] = y;
                map_alive = false;
                break;
            }
        }
        default:
            move_x = 0;
            move_y = 0;
            if (travel_mode)
            {
                map_alive = false;
                break;
            }
            redraw_map = false;
            continue;
        }

        if (!map_alive)
            break;

        if (curs_x + move_x < 1 || curs_x + move_x > 80)
            move_x = 0;

        curs_x += move_x;

        if (num_lines < map_lines)
        {
            // Scrolling only happens when we don't have a large enough 
            // display to show the known map.
            if (getty == '-' || getty == '+')
            {
                if (getty == '-')
                    screen_y -= 20;

                if (screen_y <= min_y + half_screen)
                    screen_y = min_y + half_screen;

                if (getty == '+')
                    screen_y += 20;

                if (screen_y >= max_y - half_screen)
                    screen_y = max_y - half_screen;

                continue;
            }

            if (curs_y + move_y < 1)
            {
                // screen_y += (curs_y + move_y) - 1;
                screen_y += move_y;

                if (screen_y < min_y + half_screen) {
                    move_y   = screen_y - (min_y + half_screen);
                    screen_y = min_y + half_screen;
                }
                else
                    move_y = 0;
            }

            if (curs_y + move_y > num_lines - 1)
            {
                // screen_y += (curs_y + move_y) - num_lines + 1;
                screen_y += move_y;

                if (screen_y > max_y - half_screen) {
                    move_y   = screen_y - (max_y - half_screen);
                    screen_y = max_y - half_screen;
                }
                else
                    move_y = 0;
            }
        }

        if (curs_y + move_y < 1 || curs_y + move_y > num_lines)
            move_y = 0;

        curs_y += move_y;
    }

#ifdef DOS_TERM
    puttext(1, 1, 80, 25, buffer);
#endif

    return;
}                               // end show_map()


void magic_mapping(int map_radius, int proportion)
{
    int i, j, k, l, empty_count;

    if (map_radius > 50 && map_radius != 1000)
        map_radius = 50;
    else if (map_radius < 5)
        map_radius = 5;

    // now gradually weaker with distance:
    const int pfar = (map_radius * 7) / 10;
    const int very_far = (map_radius * 9) / 10;

    for (i = you.x_pos - map_radius; i < you.x_pos + map_radius; i++)
    {
        for (j = you.y_pos - map_radius; j < you.y_pos + map_radius; j++)
        {
            if (proportion < 100 && random2(100) >= proportion)
                continue;       // note that proportion can be over 100

            if (!map_bounds(i, j))
                continue;

            const int dist = grid_distance( you.x_pos, you.y_pos, i, j );

            if (dist > pfar && one_chance_in(3))
                continue;

            if (dist > very_far && coinflip())
                continue;

            if (is_terrain_changed(i, j))
                clear_envmap_grid(i, j);

            if (is_terrain_known(i, j))
                continue;

            empty_count = 8;

            if (grid_is_solid(grd[i][j]) && grd[i][j] != DNGN_CLOSED_DOOR)
            {
                for (k = -1; k <= 1; k++)
                {
                    for (l = -1; l <= 1; l++)
                    {
                        if (k == 0 && l == 0)
                            continue;

                        if (!map_bounds( i + k, j + l ))
                        {
                            --empty_count;
                            continue;
                        }

                        if (grid_is_solid( grd[i + k][j + l] )
                                && grd[i + k][j + l] != DNGN_CLOSED_DOOR)
                            empty_count--;
                    }
                }
            }

            if (empty_count > 0)
            {
                if (!get_envmap_char(i, j))
                    set_envmap_char(i, j, get_magicmap_char(grd[i][j]));

                // Hack to give demonspawn Pandemonium mutation the ability
                // to detect exits magically.
                if ((you.mutation[MUT_PANDEMONIUM] > 1
                        && grd[i][j] == DNGN_EXIT_PANDEMONIUM)
                        // Wizmode
                        || map_radius == 1000)
                    set_terrain_seen( i, j );
                else
                    set_terrain_mapped( i, j );
            }
        }
    }
}                               // end magic_mapping()

// realize that this is simply a repackaged version of
// stuff::see_grid() -- make certain they correlate {dlb}:
bool mons_near(struct monsters *monster, unsigned int foe)
{
    // early out -- no foe!
    if (foe == MHITNOT)
        return (false);

    if (foe == MHITYOU)
    {
        if (monster->x > you.x_pos - 9 && monster->x < you.x_pos + 9
            && monster->y > you.y_pos - 9 && monster->y < you.y_pos + 9)
        {
            if (env.show[monster->x - you.x_pos + 9][monster->y - you.y_pos + 9])
                return (true);
        }
        return (false);
    }

    // must be a monster
    struct monsters *myFoe = &menv[foe];
    if (myFoe->type >= 0)
    {
        if (monster->x > myFoe->x - 9 && monster->x < myFoe->x + 9
            && monster->y > myFoe->y - 9 && monster->y < myFoe->y + 9)
        {
            return (true);
        }
    }

    return (false);
}                               // end mons_near()

// answers the question: "Is a grid within character's line of sight?"
bool see_grid( int grx, int gry )
{
    // rare case: can player see self?  (of course!)
    if (grx == you.x_pos && gry == you.y_pos)
        return (true);

    // check env.show array
    if (grid_distance( grx, gry, you.x_pos, you.y_pos ) < 9)
    {
        const int ex = grx - you.x_pos + 9;
        const int ey = gry - you.y_pos + 9;

        if (env.show[ex][ey])
            return (true);
    }

    return (false);
}  // end see_grid() 

static const unsigned char table[ NUM_CSET ][ NUM_DCHAR_TYPES ] = 
{
    // CSET_ASCII
    {
        '#', '*', '.', ',', '\'', '+', '^', '>', '<',  // wall, stairs up
        '_', '\\', '}', '{', '8', '~', '~',            // altar, item detect
        '0', ')', '[', '/', '%', '?', '=', '!', '(',   // orb, missile
        ':', '|', '}', '%', '$', '"', '#',             // book, cloud
    },

    // CSET_IBM - this is ANSI 437
    {
        177, 176, 249, 250, '\'', 254, '^', '>', '<',  // wall, stairs up
        220, 239, 244, 247, '8', '~', '~',             // altar, item detect
        '0', ')', '[', '/', '%', '?', '=', '!', '(',   // orb, missile
        '+', '\\', '}', '%', '$', '"', '#',            // book, cloud
    },

    // CSET_DEC - remember: 224-255 are mapped to shifted 96-127
    {
        225, 224, 254, ':', '\'', 238, '^', '>', '<',  // wall, stairs up
        251, 182, 167, 187, '8', 171, 168,             // altar, item detect
        '0', ')', '[', '/', '%', '?', '=', '!', '(',   // orb, missile
        '+', '\\', '}', '%', '$', '"', '#',            // book, cloud
    },
};

dungeon_char_type dchar_by_name(const std::string &name)
{
    const char *dchar_names[] =
    {
        "wall", "wall_magic", "floor", "floor_magic", "door_open",
        "door_closed", "trap", "stairs_down", "stairs_up", "altar", "arch",
        "fountain", "wavy", "statue", "invis_exposed", "item_detected",
        "item_orb", "item_weapon", "item_armour", "item_wand", "item_food",
        "item_scroll", "item_ring", "item_potion", "item_missile", "item_book",
        "item_stave", "item_miscellany", "item_corpse", "item_gold",
        "item_amulet", "cloud"
    };
    for (unsigned i = 0; i < sizeof(dchar_names) / sizeof(*dchar_names); ++i)
    {
        if (dchar_names[i] == name)
            return dungeon_char_type(i);
    }
    return (NUM_DCHAR_TYPES);
}

void init_char_table( char_set_type set )
{
    for (int i = 0; i < NUM_DCHAR_TYPES; i++)
    {
        if (Options.cset_override[set][i])
            Options.char_table[i] = Options.cset_override[set][i];
        else
            Options.char_table[i] = table[set][i];
    }
}

void apply_feature_overrides()
{
    for (int i = 0, size = Options.feature_overrides.size(); i < size; ++i)
    {
        const feature_override      &fov    = Options.feature_overrides[i];
        const feature_def           &ofeat  = fov.override;
        feature_def                 &feat   = Feature[fov.feat];

        if (ofeat.symbol)
            feat.symbol = ofeat.symbol;
        if (ofeat.magic_symbol)
            feat.magic_symbol = ofeat.magic_symbol;
        if (ofeat.colour)
            feat.colour = ofeat.colour;
        if (ofeat.map_colour)
            feat.map_colour = ofeat.map_colour;
        if (ofeat.seen_colour)
            feat.seen_colour = ofeat.seen_colour;
    }
}

void init_feature_table( void )
{
    for (int i = 0; i < NUM_FEATURES; i++)
    {
        Feature[i].symbol = 0;
        Feature[i].colour = BLACK;      // means must be set some other way
        Feature[i].notable = false;
        Feature[i].seen_effect = false;
        Feature[i].magic_symbol = 0;    // made equal to symbol if untouched
        Feature[i].map_colour = DARKGREY;
        Feature[i].seen_colour = BLACK;    // marks no special seen map handling

        switch (i)
        {
        case DNGN_UNSEEN:
        default:
            break;

        case DNGN_ROCK_WALL:
        case DNGN_PERMAROCK_WALL:
            Feature[i].symbol = Options.char_table[ DCHAR_WALL ];
            Feature[i].colour = EC_ROCK;
            Feature[i].magic_symbol = Options.char_table[ DCHAR_WALL_MAGIC ];
            break;

        case DNGN_STONE_WALL:
            Feature[i].symbol = Options.char_table[ DCHAR_WALL ];
            Feature[i].colour = EC_STONE;
            Feature[i].magic_symbol = Options.char_table[ DCHAR_WALL_MAGIC ];
            break;

        case DNGN_OPEN_DOOR:
            Feature[i].symbol = Options.char_table[ DCHAR_DOOR_OPEN ];
            Feature[i].colour = LIGHTGREY;
            break;

        case DNGN_CLOSED_DOOR:
            Feature[i].symbol = Options.char_table[ DCHAR_DOOR_CLOSED ];
            Feature[i].colour = LIGHTGREY;
            break;

        case DNGN_METAL_WALL:
            Feature[i].symbol = Options.char_table[ DCHAR_WALL ];
            Feature[i].colour = CYAN;
            Feature[i].magic_symbol = Options.char_table[ DCHAR_WALL_MAGIC ];
            break;

        case DNGN_SECRET_DOOR:
            // Note: get_secret_door_appearance means this probably isn't used
            Feature[i].symbol = Options.char_table[ DCHAR_WALL ];
            Feature[i].colour = EC_ROCK;
            Feature[i].magic_symbol = Options.char_table[ DCHAR_WALL_MAGIC ];
            break;

        case DNGN_GREEN_CRYSTAL_WALL:
            Feature[i].symbol = Options.char_table[ DCHAR_WALL ];
            Feature[i].colour = GREEN;
            Feature[i].magic_symbol = Options.char_table[ DCHAR_WALL_MAGIC ];
            break;

        case DNGN_ORCISH_IDOL:
            Feature[i].symbol = Options.char_table[ DCHAR_STATUE ];
            Feature[i].colour = DARKGREY;
            break;

        case DNGN_WAX_WALL:
            Feature[i].symbol = Options.char_table[ DCHAR_WALL ];
            Feature[i].colour = YELLOW;
            Feature[i].magic_symbol = Options.char_table[ DCHAR_WALL_MAGIC ];
            break;                  // wax wall

        case DNGN_SILVER_STATUE:
            Feature[i].symbol = Options.char_table[ DCHAR_STATUE ];
            Feature[i].colour = WHITE;
            Feature[i].seen_effect = true;
            break;

        case DNGN_GRANITE_STATUE:
            Feature[i].symbol = Options.char_table[ DCHAR_STATUE ];
            Feature[i].colour = LIGHTGREY;
            break;

        case DNGN_ORANGE_CRYSTAL_STATUE:
            Feature[i].symbol = Options.char_table[ DCHAR_STATUE ];
            Feature[i].colour = LIGHTRED;
            Feature[i].seen_effect = true;
            break;

        case DNGN_LAVA:
            Feature[i].symbol = Options.char_table[ DCHAR_WAVY ];
            Feature[i].colour = RED;
            break;

        case DNGN_DEEP_WATER:
            Feature[i].symbol = Options.char_table[ DCHAR_WAVY ];
            Feature[i].colour = BLUE;
            break;

        case DNGN_SHALLOW_WATER:
            Feature[i].symbol = Options.char_table[ DCHAR_WAVY ];
            Feature[i].colour = CYAN;
            break;

        case DNGN_FLOOR:
            Feature[i].symbol = Options.char_table[ DCHAR_FLOOR ];
            Feature[i].colour = EC_FLOOR;
            Feature[i].magic_symbol = Options.char_table[ DCHAR_FLOOR_MAGIC ];
            break;

        case DNGN_EXIT_HELL:
            Feature[i].symbol = Options.char_table[ DCHAR_ARCH ];
            Feature[i].colour = LIGHTRED;
            Feature[i].notable = false;
            Feature[i].map_colour = LIGHTGREY;
            Feature[i].seen_colour = LIGHTRED;
            break;

        case DNGN_ENTER_HELL:
            Feature[i].symbol = Options.char_table[ DCHAR_ARCH ];
            Feature[i].colour = RED;
            Feature[i].notable = true;
            Feature[i].map_colour = LIGHTGREY;
            Feature[i].seen_colour = RED;
            break;

        case DNGN_TRAP_MECHANICAL:
            Feature[i].colour = LIGHTCYAN;
            Feature[i].symbol = Options.char_table[ DCHAR_TRAP ];
            Feature[i].map_colour = LIGHTCYAN;
            break;

        case DNGN_TRAP_MAGICAL:
            Feature[i].colour = MAGENTA;
            Feature[i].symbol = Options.char_table[ DCHAR_TRAP ];
            Feature[i].map_colour = MAGENTA;
            break;

        case DNGN_TRAP_III:
            Feature[i].colour = LIGHTGREY;
            Feature[i].symbol = Options.char_table[ DCHAR_TRAP ];
            Feature[i].map_colour = LIGHTGREY;
            break;

        case DNGN_UNDISCOVERED_TRAP:
            Feature[i].symbol = Options.char_table[ DCHAR_FLOOR ];
            Feature[i].colour = EC_FLOOR;
            Feature[i].magic_symbol = Options.char_table[ DCHAR_FLOOR_MAGIC ];
            break;

        case DNGN_ENTER_SHOP:
            Feature[i].symbol = Options.char_table[ DCHAR_ARCH ];
            Feature[i].colour = YELLOW;
            Feature[i].notable = true;
            Feature[i].map_colour = LIGHTGREY;
            Feature[i].seen_colour = YELLOW;
            break;

        case DNGN_ENTER_LABYRINTH:
            Feature[i].symbol = Options.char_table[ DCHAR_ARCH ];
            Feature[i].colour = CYAN;
            Feature[i].notable = true;
            Feature[i].map_colour = LIGHTGREY;
            Feature[i].seen_colour = CYAN;
            break;

        case DNGN_ROCK_STAIRS_DOWN:
        case DNGN_STONE_STAIRS_DOWN_I:
        case DNGN_STONE_STAIRS_DOWN_II:
        case DNGN_STONE_STAIRS_DOWN_III:
            Feature[i].symbol = Options.char_table[ DCHAR_STAIRS_DOWN ];
            Feature[i].colour = ((i == DNGN_ROCK_STAIRS_DOWN) ? BROWN 
                                                              : LIGHTGREY);
            Feature[i].map_colour = RED;
            break;

        case DNGN_ROCK_STAIRS_UP:
        case DNGN_STONE_STAIRS_UP_I:
        case DNGN_STONE_STAIRS_UP_II:
        case DNGN_STONE_STAIRS_UP_III:
            Feature[i].symbol = Options.char_table[ DCHAR_STAIRS_UP ];
            Feature[i].colour = ((i == DNGN_ROCK_STAIRS_UP) ? BROWN 
                                                            : LIGHTGREY);
            Feature[i].map_colour = GREEN;
            break;

        case DNGN_ENTER_DIS:
            Feature[i].colour = CYAN;
            Feature[i].symbol = Options.char_table[ DCHAR_ARCH ];
            Feature[i].notable = true;
            Feature[i].map_colour = LIGHTGREY;
            Feature[i].seen_colour = CYAN;
            break;

        case DNGN_ENTER_GEHENNA:
            Feature[i].colour = RED;
            Feature[i].symbol = Options.char_table[ DCHAR_ARCH ];
            Feature[i].notable = true;
            Feature[i].map_colour = LIGHTGREY;
            Feature[i].seen_colour = RED;
            break;

        case DNGN_ENTER_COCYTUS:
            Feature[i].colour = LIGHTCYAN;
            Feature[i].symbol = Options.char_table[ DCHAR_ARCH ];
            Feature[i].notable = true;
            Feature[i].map_colour = LIGHTGREY;
            Feature[i].seen_colour = LIGHTCYAN;
            break;

        case DNGN_ENTER_TARTARUS:
            Feature[i].colour = DARKGREY;
            Feature[i].symbol = Options.char_table[ DCHAR_ARCH ];
            Feature[i].notable = true;
            Feature[i].map_colour = LIGHTGREY;
            Feature[i].seen_colour = DARKGREY;
            break;

        case DNGN_ENTER_ABYSS:
            Feature[i].colour = EC_RANDOM;
            Feature[i].symbol = Options.char_table[ DCHAR_ARCH ];
            Feature[i].notable = true;
            Feature[i].map_colour = LIGHTGREY;
            Feature[i].seen_colour = EC_RANDOM;
            break;

        case DNGN_EXIT_ABYSS:
            Feature[i].colour = EC_RANDOM;
            Feature[i].symbol = Options.char_table[ DCHAR_ARCH ];
            Feature[i].map_colour = EC_RANDOM;
            break;

        case DNGN_STONE_ARCH:
            Feature[i].colour = LIGHTGREY;
            Feature[i].symbol = Options.char_table[ DCHAR_ARCH ];
            Feature[i].map_colour = LIGHTGREY;
            break;

        case DNGN_ENTER_PANDEMONIUM:
            Feature[i].colour = LIGHTBLUE;
            Feature[i].symbol = Options.char_table[ DCHAR_ARCH ];
            Feature[i].notable = true;
            Feature[i].map_colour = LIGHTGREY;
            Feature[i].seen_colour = LIGHTBLUE;
            break;

        case DNGN_EXIT_PANDEMONIUM:
            // Note: has special handling for colouring with mutation
            Feature[i].colour = LIGHTBLUE;
            Feature[i].symbol = Options.char_table[ DCHAR_ARCH ];
            Feature[i].map_colour = LIGHTGREY;
            Feature[i].seen_colour = LIGHTBLUE;
            break;

        case DNGN_TRANSIT_PANDEMONIUM:
            Feature[i].colour = LIGHTGREEN;
            Feature[i].symbol = Options.char_table[ DCHAR_ARCH ];
            Feature[i].map_colour = LIGHTGREY;
            Feature[i].seen_colour = LIGHTGREEN;
            break;

        case DNGN_ENTER_ORCISH_MINES:
        case DNGN_ENTER_HIVE:
        case DNGN_ENTER_LAIR:
        case DNGN_ENTER_SLIME_PITS:
        case DNGN_ENTER_VAULTS:
        case DNGN_ENTER_CRYPT:
        case DNGN_ENTER_HALL_OF_BLADES:
        case DNGN_ENTER_TEMPLE:
        case DNGN_ENTER_SNAKE_PIT:
        case DNGN_ENTER_ELVEN_HALLS:
        case DNGN_ENTER_TOMB:
        case DNGN_ENTER_SWAMP:
        case DNGN_ENTER_RESERVED_1:
        case DNGN_ENTER_RESERVED_2:
        case DNGN_ENTER_RESERVED_3:
        case DNGN_ENTER_RESERVED_4:
            Feature[i].colour = YELLOW;
            Feature[i].symbol = Options.char_table[ DCHAR_STAIRS_DOWN ];
            Feature[i].notable = true;
            Feature[i].map_colour = RED;
            Feature[i].seen_colour = LIGHTRED;
            break;

        case DNGN_ENTER_ZOT:
            Feature[i].colour = MAGENTA;
            Feature[i].symbol = Options.char_table[ DCHAR_ARCH ];
            Feature[i].notable = true;
            Feature[i].map_colour = LIGHTGREY;
            Feature[i].seen_colour = MAGENTA;
            break;

        case DNGN_RETURN_FROM_ORCISH_MINES:
        case DNGN_RETURN_FROM_HIVE:
        case DNGN_RETURN_FROM_LAIR:
        case DNGN_RETURN_FROM_SLIME_PITS:
        case DNGN_RETURN_FROM_VAULTS:
        case DNGN_RETURN_FROM_CRYPT:
        case DNGN_RETURN_FROM_HALL_OF_BLADES:
        case DNGN_RETURN_FROM_TEMPLE:
        case DNGN_RETURN_FROM_SNAKE_PIT:
        case DNGN_RETURN_FROM_ELVEN_HALLS:
        case DNGN_RETURN_FROM_TOMB:
        case DNGN_RETURN_FROM_SWAMP:
        case DNGN_RETURN_RESERVED_1:
        case DNGN_RETURN_RESERVED_2:
        case DNGN_RETURN_RESERVED_3:
        case DNGN_RETURN_RESERVED_4:
            Feature[i].colour = YELLOW;
            Feature[i].symbol = Options.char_table[ DCHAR_STAIRS_UP ];
            Feature[i].map_colour = BLUE;
            Feature[i].seen_colour = LIGHTBLUE;
            break;

        case DNGN_RETURN_FROM_ZOT:
            Feature[i].colour = MAGENTA;
            Feature[i].symbol = Options.char_table[ DCHAR_ARCH ];
            Feature[i].map_colour = LIGHTGREY;
            Feature[i].seen_colour = MAGENTA;
            break;

        case DNGN_ALTAR_ZIN:
            Feature[i].colour = WHITE;
            Feature[i].symbol = Options.char_table[ DCHAR_ALTAR ];
            Feature[i].notable = true;
            Feature[i].map_colour = DARKGREY;
            Feature[i].seen_colour = WHITE;
            break;

        case DNGN_ALTAR_SHINING_ONE:
            Feature[i].colour = YELLOW;
            Feature[i].symbol = Options.char_table[ DCHAR_ALTAR ];
            Feature[i].notable = true;
            Feature[i].map_colour = DARKGREY;
            Feature[i].seen_colour = YELLOW;
            break;

        case DNGN_ALTAR_KIKUBAAQUDGHA:
            Feature[i].colour = DARKGREY;
            Feature[i].symbol = Options.char_table[ DCHAR_ALTAR ];
            Feature[i].notable = true;
            Feature[i].map_colour = DARKGREY;
            Feature[i].seen_colour = DARKGREY;
            break;

        case DNGN_ALTAR_YREDELEMNUL:
            Feature[i].colour = EC_UNHOLY;
            Feature[i].symbol = Options.char_table[ DCHAR_ALTAR ];
            Feature[i].notable = true;
            Feature[i].map_colour = DARKGREY;
            Feature[i].seen_colour = EC_UNHOLY;
            break;

        case DNGN_ALTAR_XOM:
            Feature[i].colour = EC_RANDOM;
            Feature[i].symbol = Options.char_table[ DCHAR_ALTAR ];
            Feature[i].notable = true;
            Feature[i].map_colour = DARKGREY;
            Feature[i].seen_colour = EC_RANDOM;
            break;

        case DNGN_ALTAR_VEHUMET:
            Feature[i].colour = EC_VEHUMET;
            Feature[i].symbol = Options.char_table[ DCHAR_ALTAR ];
            Feature[i].notable = true;
            Feature[i].map_colour = DARKGREY;
            Feature[i].seen_colour = EC_VEHUMET;
            break;

        case DNGN_ALTAR_OKAWARU:
            Feature[i].colour = CYAN;
            Feature[i].symbol = Options.char_table[ DCHAR_ALTAR ];
            Feature[i].notable = true;
            Feature[i].map_colour = DARKGREY;
            Feature[i].seen_colour = CYAN;
            break;

        case DNGN_ALTAR_MAKHLEB:
            Feature[i].colour = EC_FIRE;
            Feature[i].symbol = Options.char_table[ DCHAR_ALTAR ];
            Feature[i].notable = true;
            Feature[i].map_colour = DARKGREY;
            Feature[i].seen_colour = EC_FIRE;
            break;

        case DNGN_ALTAR_SIF_MUNA:
            Feature[i].colour = BLUE;
            Feature[i].symbol = Options.char_table[ DCHAR_ALTAR ];
            Feature[i].notable = true;
            Feature[i].map_colour = DARKGREY;
            Feature[i].seen_colour = BLUE;
            break;

        case DNGN_ALTAR_TROG:
            Feature[i].colour = RED;
            Feature[i].symbol = Options.char_table[ DCHAR_ALTAR ];
            Feature[i].notable = true;
            Feature[i].map_colour = DARKGREY;
            Feature[i].seen_colour = RED;
            break;

        case DNGN_ALTAR_NEMELEX_XOBEH:
            Feature[i].colour = LIGHTMAGENTA;
            Feature[i].symbol = Options.char_table[ DCHAR_ALTAR ];
            Feature[i].notable = true;
            Feature[i].map_colour = DARKGREY;
            Feature[i].seen_colour = LIGHTMAGENTA;
            break;

        case DNGN_ALTAR_ELYVILON:
            Feature[i].colour = LIGHTGREY;
            Feature[i].symbol = Options.char_table[ DCHAR_ALTAR ];
            Feature[i].notable = true;
            Feature[i].map_colour = DARKGREY;
            Feature[i].seen_colour = LIGHTGREY;
            break;

        case DNGN_ALTAR_LUCY:
            Feature[i].colour = GREEN;
            Feature[i].symbol = Options.char_table[ DCHAR_ALTAR ];
            Feature[i].notable = true;
            Feature[i].map_colour = DARKGREY;
            Feature[i].seen_colour = GREEN;
            break;

        case DNGN_BLUE_FOUNTAIN:
            Feature[i].colour = BLUE;
            Feature[i].symbol = Options.char_table[ DCHAR_FOUNTAIN ];
            break;

        case DNGN_SPARKLING_FOUNTAIN:
            Feature[i].colour = LIGHTBLUE;
            Feature[i].symbol = Options.char_table[ DCHAR_FOUNTAIN ];
            break;

        case DNGN_DRY_FOUNTAIN_I:
        case DNGN_DRY_FOUNTAIN_II:
        case DNGN_PERMADRY_FOUNTAIN:
            Feature[i].colour = LIGHTGREY;
            Feature[i].symbol = Options.char_table[ DCHAR_FOUNTAIN ];
            break;

        case DNGN_INVIS_EXPOSED:
            Feature[i].symbol = Options.char_table[ DCHAR_INVIS_EXPOSED ];
            break;

        case DNGN_ITEM_DETECTED:
            Feature[i].magic_symbol = Options.char_table[ DCHAR_ITEM_DETECTED ];
            break;

        case DNGN_ITEM_ORB:
            Feature[i].symbol = Options.char_table[ DCHAR_ITEM_ORB ];
            break;

        case DNGN_ITEM_WEAPON:
            Feature[i].symbol = Options.char_table[ DCHAR_ITEM_WEAPON ];
            break;

        case DNGN_ITEM_ARMOUR:
            Feature[i].symbol = Options.char_table[ DCHAR_ITEM_ARMOUR ];
            break;

        case DNGN_ITEM_WAND:
            Feature[i].symbol = Options.char_table[ DCHAR_ITEM_WAND ];
            break;

        case DNGN_ITEM_FOOD:
            Feature[i].symbol = Options.char_table[ DCHAR_ITEM_FOOD ];
            break;

        case DNGN_ITEM_SCROLL:
            Feature[i].symbol = Options.char_table[ DCHAR_ITEM_SCROLL ];
            break;

        case DNGN_ITEM_RING:
            Feature[i].symbol = Options.char_table[ DCHAR_ITEM_RING ];
            break;

        case DNGN_ITEM_POTION:
            Feature[i].symbol = Options.char_table[ DCHAR_ITEM_POTION ];
            break;

        case DNGN_ITEM_MISSILE:
            Feature[i].symbol = Options.char_table[ DCHAR_ITEM_MISSILE ];
            break;

        case DNGN_ITEM_BOOK:
            Feature[i].symbol = Options.char_table[ DCHAR_ITEM_BOOK ];
            break;

        case DNGN_ITEM_STAVE:
            Feature[i].symbol = Options.char_table[ DCHAR_ITEM_STAVE ];
            break;

        case DNGN_ITEM_MISCELLANY:
            Feature[i].symbol = Options.char_table[ DCHAR_ITEM_MISCELLANY ];
            break;

        case DNGN_ITEM_CORPSE:
            Feature[i].symbol = Options.char_table[ DCHAR_ITEM_CORPSE ];
            break;

        case DNGN_ITEM_GOLD:
            Feature[i].symbol = Options.char_table[ DCHAR_ITEM_GOLD ];
            break;

        case DNGN_ITEM_AMULET:
            Feature[i].symbol = Options.char_table[ DCHAR_ITEM_AMULET ];
            break;

        case DNGN_CLOUD:
            Feature[i].symbol = Options.char_table[ DCHAR_CLOUD ];
            break;
        }
    }

    apply_feature_overrides();

    for (int i = 0; i < NUM_FEATURES; ++i)
    {
        if (!Feature[i].magic_symbol)
            Feature[i].magic_symbol = Feature[i].symbol;

        if (Feature[i].seen_colour == BLACK)
            Feature[i].seen_colour = Feature[i].map_colour;
    }
}

static int get_screen_glyph( int x, int y ) 
{
    const int ex = x - you.x_pos + 9;
    const int ey = y - you.y_pos + 9;

    int             object = env.show[ex][ey];
    unsigned short  colour = env.show_col[ex][ey];
    unsigned short  ch;

    if (!object)
        return get_envmap_char(x, y);

    if (object == DNGN_SECRET_DOOR)
        object = grid_secret_door_appearance( x, y );

    get_symbol( x, y, object, &ch, &colour );
    return (ch);
}

// Returns a string containing an ASCII representation of the map. If fullscreen
// is set to false, only the viewable area is returned. Leading and trailing 
// spaces are trimmed from each line. Leading and trailing empty lines are also
// snipped.
std::string screenshot( bool fullscreen )
{
    UNUSED( fullscreen );

    const int X_SIZE = VIEW_WIDTH;
    const int Y_SIZE = VIEW_HEIGHT;

    // [ds] Screenshots need to be straight ASCII. We will now proceed to force
    // the char and feature tables back to ASCII.
    FixedVector<unsigned char, NUM_DCHAR_TYPES> char_table_bk;
    char_table_bk = Options.char_table;

    init_char_table(CSET_ASCII);
    init_feature_table();
    
    int firstnonspace = -1;
    int firstpopline  = -1;
    int lastpopline   = -1;

    char lines[Y_SIZE][X_SIZE + 1];
    for (int count_y = 0; count_y < Y_SIZE; count_y++)
    {
        int lastnonspace = -1;
        
        for (int count_x = 0; count_x < X_SIZE; count_x++)
        {
            // in grid coords
            const int gx = count_x + you.x_pos - 16;
            const int gy = count_y + you.y_pos - 8;

            int ch = (!map_bounds(gx, gy)) 
                        ? 0 
                        : (count_x < 8 || count_x > 24) 
                            ? get_envmap_char(gx, gy) 
                            : (gx == you.x_pos && gy == you.y_pos)
                                ? you.symbol 
                                : get_screen_glyph(gx, gy);

            if (ch && !isprint(ch)) 
            {
                // [ds] Evil hack time again. Peek at grid, use that character.
                int object = grd[gx][gy];
                unsigned short glych, glycol = 0;

                if (object == DNGN_SECRET_DOOR)
                    object = grid_secret_door_appearance( gx, gy );

                get_symbol( gx, gy, object, &glych, &glycol );
                ch = glych;
            }
            
            // More mangling to accommodate C strings.
            if (!ch)
                ch = ' ';

            if (ch != ' ') 
            {
                lastnonspace = count_x;
                lastpopline = count_y;

                if (firstnonspace == -1 || firstnonspace > count_x)
                    firstnonspace = count_x;

                if (firstpopline == -1)
                    firstpopline = count_y;
            }

            lines[count_y][count_x] = ch;
        }

        lines[count_y][lastnonspace + 1] = 0;
    }

    // Restore char and feature tables
    Options.char_table = char_table_bk;
    init_feature_table();

    std::string ss;
    if (firstpopline != -1 && lastpopline != -1) 
    {
        if (firstnonspace == -1)
            firstnonspace = 0;

        for (int i = firstpopline; i <= lastpopline; ++i) 
        {
            char *curr = lines[i];

            while (*curr && curr - lines[i] < firstnonspace)
                curr++;

            ss += curr;
            ss += EOL;
        }
    }

    return (ss);
}

static int viewmap_flash_colour()
{
    if (you.special_wield == SPWLD_SHADOW)
        return (DARKGREY);
    else if (you.berserker)
        return (RED);

    return (BLACK);
}

//---------------------------------------------------------------
//
// viewwindow -- now unified and rolled into a single pass
//
// Draws the main window using the character set returned 
// by get_symbol().
//
// This function should not interfere with the game condition,
// unless do_updates is set (ie. stealth checks for visible
// monsters).
//
//---------------------------------------------------------------
void viewwindow(bool draw_it, bool do_updates)
{
    const int X_SIZE = VIEW_WIDTH;
    const int Y_SIZE = VIEW_HEIGHT;
    const int BUFFER_SIZE = 1550;

    FixedVector < screen_buffer_t, BUFFER_SIZE > buffy;
    int count_x, count_y;

    losight( env.show, grd, you.x_pos, you.y_pos ); // must be done first

    for (count_x = 0; count_x < NUM_STATUE_TYPES; count_x++)
        you.visible_statue[count_x] = 0;

    for (count_x = 0; count_x < 18; count_x++)
    {
        for (count_y = 0; count_y < 18; count_y++)
        {
            env.show_col[count_x][count_y] = LIGHTGREY;
            Show_Backup[count_x][count_y] = 0;
        }
    }

    item_grid();                // must be done before cloud and monster
    cloud_grid();
    monster_grid( do_updates );

    if (draw_it)
    {
        cursor_control cs(false);

        const bool map = player_in_mappable_area();
        int bufcount = 0;

        int flash_colour = you.flash_colour;
        if (flash_colour == BLACK)
            flash_colour = viewmap_flash_colour();

        for (count_y = 0; count_y < Y_SIZE; count_y++)
        {
            for (count_x = 0; count_x < X_SIZE; count_x++)
            {
                // in grid coords
                const int gx = count_x + you.x_pos - 16;
                const int gy = count_y + you.y_pos - 8;

                // order is important here
                if (!map_bounds( gx, gy ))
                {
                    // off the map
                    buffy[bufcount] = 0;
                    buffy[bufcount + 1] = DARKGREY;
                }
                else if (count_x < 8 || count_x > 24)
                {
                    // outside the env.show area
                    buffy[bufcount] = get_envmap_char( gx, gy );
                    buffy[bufcount + 1] = DARKGREY;

                    if (Options.colour_map)
                        buffy[bufcount + 1] = 
                            colour_code_map(gx - 1, gy - 1, 
                                            Options.item_colour);
                }
                else if (gx == you.x_pos && gy == you.y_pos)
                {
                    const int ex = gx - you.x_pos + 9;
                    const int ey = gy - you.y_pos + 9;

                    int             object = env.show[ex][ey];
                    unsigned short  colour = env.show_col[ex][ey];
                    unsigned short  ch;
                    get_symbol( gx, gy, object, &ch, &colour );

                    if (map)
                    {
                        set_envmap_glyph( gx, gy, ch, colour, object );
                        set_terrain_seen( gx, gy );
                        set_envmap_detected_mons(gx, gy, false);
                        set_envmap_detected_item(gx, gy, false);
                    }

                    // player overrides everything in cell
                    buffy[bufcount] = you.symbol;
                    buffy[bufcount + 1] = you.colour;

                    if (player_is_swimming())
                    {
                        if (grd[gx][gy] == DNGN_DEEP_WATER)
                            buffy[bufcount + 1] = BLUE;
                        else 
                            buffy[bufcount + 1] = CYAN;
                    }
                }
                else
                {
                    // Note: env.show is set for grids in LoS
                    // get env coords
                    const int ex = gx - you.x_pos + 9;
                    const int ey = gy - you.y_pos + 9;

                    int             object = env.show[ex][ey];
                    unsigned short  colour = env.show_col[ex][ey];
                    unsigned short  ch;

                    if (object == DNGN_SECRET_DOOR)
                        object = grid_secret_door_appearance( gx, gy );

                    get_symbol( gx, gy, object, &ch, &colour );

                    buffy[bufcount] = ch;
                    buffy[bufcount + 1] = colour;

                    if (map)
                    {
                        // This section is very tricky because it 
                        // duplicates the old code (which was horrid).
                        
                        // if the grid is in LoS env.show was set and
                        // we set the buffer already, so...
                        if (buffy[bufcount] != 0)
                        {
                            // ... map that we've seen this
                            set_envmap_glyph( gx, gy, ch, colour, object );
                            set_terrain_seen( gx, gy );
                            set_envmap_detected_mons(gx, gy, false);
                            set_envmap_detected_item(gx, gy, false);
                        }

                        // Check if we're looking to clean_map...
                        // but don't touch the buffer to clean it, 
                        // instead we modify the env.map itself so
                        // that the map stays clean as it moves out
                        // of the env.show radius.
                        //
                        // Note: show_backup is 0 on every square which
                        // is inside the env.show radius and doesn't
                        // have a monster or cloud on it, and is equal
                        // to the grid before monsters and clouds were
                        // added otherwise.
                        if (Options.clean_map 
                            && Show_Backup[ex][ey]
                            && is_terrain_seen( gx, gy ))
                        {
                            get_symbol( gx, gy,
                                        Show_Backup[ex][ey], &ch, &colour );
                            set_envmap_glyph( gx, gy, ch, colour, 
                                              Show_Backup[ex][ey] );
                        }

                        // Now we get to filling in both the unseen 
                        // grids in the env.show radius area as 
                        // well doing the clean_map.  The clean_map
                        // is done by having the env.map set to the
                        // backup character above, and down here we
                        // procede to override that character if it's
                        // out of LoS!  If it wasn't, buffy would have
                        // already been set (but we'd still have 
                        // clobbered env.map... which is important
                        // to do for when we move away from the area!)
                        if (buffy[bufcount] == 0)
                        {
                            // show map
                            buffy[bufcount] = get_envmap_char( gx, gy );
                            buffy[bufcount + 1] = DARKGREY;

                            if (Options.colour_map)
                                buffy[bufcount + 1] = 
                                    colour_code_map(gx - 1, gy - 1,
                                                    Options.item_colour);
                        }
                    }
                }
                
                // alter colour if flashing the characters vision
                if (flash_colour != BLACK 
                    && buffy[bufcount + 1] != DARKGREY)
                {
                    buffy[bufcount + 1] = flash_colour;
                }

                bufcount += 2;
            }
        }

        // Leaving it this way because short flashes can occur in long ones,
        // and this simply works without requiring a stack.
        you.flash_colour = BLACK;

#ifdef DOS_TERM
        puttext( 2, 1, X_SIZE + 1, Y_SIZE, buffy.buffer() );
#endif

#ifdef PLAIN_TERM
        // avoiding unneeded draws when running
        if (!you.running || (you.running < 0 && Options.travel_delay > -1))
        {
            gotoxy( 2, 1 );

            bufcount = 0;
            for (count_y = 0; count_y < Y_SIZE; count_y++)
            {
                for (count_x = 0; count_x < X_SIZE; count_x++)
                {
#ifdef USE_CURSES
                    buffy[bufcount] = cset_adjust( buffy[bufcount] );
#endif
                    textcolor( buffy[bufcount + 1] );
                    putch( buffy[bufcount] );
                    bufcount += 2;
                }

                gotoxy( 2, count_y + 2 );
            }
        }

#ifdef USE_CURSES
        set_altcharset( false );
#endif

#endif
    }
}                               // end viewwindow()
