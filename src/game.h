#pragma once

#include <array>
#include <chrono>
#include <ctime>
#include <functional>
#include <iosfwd>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "action.h"
#include "calendar.h"
#include "character_id.h"
#include "coordinates.h"
#include "creature.h"
#include "cursesdef.h"
#include "enums.h"
#include "game_constants.h"
#include "memory_fast.h"
#include "pimpl.h"
#include "point.h"
#include "type_id.h"

class Character;
class Creature_tracker;
class item;
class monster;
class spell_events;
class drop_token_provider;

static constexpr int DEFAULT_TILESET_ZOOM = 16;

static const std::string SAVE_MASTER( "master.gsav" );
static const std::string SAVE_ARTIFACTS( "artifacts.gsav" );
static const std::string SAVE_EXTENSION( ".sav" );
static const std::string SAVE_EXTENSION_LOG( ".log" );
static const std::string SAVE_EXTENSION_WEATHER( ".weather" );
static const std::string SAVE_EXTENSION_SHORTCUTS( ".shortcuts" );

// The reference to the one and only game instance.
class game;

extern std::unique_ptr<game> g;

extern const int savegame_version;
extern int savegame_loading_version;

class input_context;

input_context get_default_mode_input_context();

enum class dump_mode {
    TSV,
    HTML
};

enum quit_status {
    QUIT_NO = 0,    // Still playing
    QUIT_SUICIDE,   // Quit with 'Q'
    QUIT_SAVED,     // Saved and quit
    QUIT_NOSAVED,   // Quit without saving
    QUIT_DIED,      // Actual death
    QUIT_WATCH,     // Died, and watching aftermath
};

enum safe_mode_type {
    SAFE_MODE_OFF = 0, // Moving always allowed
    SAFE_MODE_ON = 1, // Moving allowed, but if a new monsters spawns, go to SAFE_MODE_STOP
    SAFE_MODE_STOP = 2, // New monsters spotted, no movement allowed
};

enum body_part : int;
enum action_id : int;

struct special_game;

class achievements_tracker;
class avatar;
class event_bus;
class faction_manager;
class kill_tracker;
class map;
class map_item_stack;
class memorial_logger;
class npc;
class player;
class save_t;
class scenario;
class stats_tracker;
template<typename Tripoint>
class tripoint_range;
class vehicle;
struct WORLDINFO;
class world;
class live_view;
class loading_ui;
class overmap;
class scent_map;
class static_popup;
class timed_event_manager;
class ui_adaptor;
struct visibility_variables;

class distribution_grid_tracker;
struct weather_printable;
class weather_manager;

using item_filter = std::function<bool ( const item & )>;

enum peek_act : int {
    PA_BLIND_THROW
    // obvious future additional value is PA_BLIND_FIRE
};

struct look_around_result {
    std::optional<tripoint> position;
    std::optional<peek_act> peek_action;
};

struct w_map {
    int id;
    std::string name;
    bool toggle;
    catacurses::window win;
};

bool is_valid_in_w_terrain( point p );

// There is only one game instance, so losing a few bytes of memory
// due to padding is not much of a concern.
// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding)
class game
{
        friend class editmap;
        friend class advanced_inventory;
        friend class main_menu;
        friend distribution_grid_tracker &get_distribution_grid_tracker();
        friend map &get_map();
        friend Character &get_player_character();
        friend avatar &get_avatar();
        friend weather_manager &get_weather();

    public:
        game();
        ~game();

        /** Loads static data that does not depend on mods or similar. */
        void load_static_data();

        /**
         * @return The current world database, or nullptr if no world is loaded.
         */
        world *get_active_world() const;

        /**
         * @brief Should be invoked whenever options change.
         */
        void on_options_changed();

    public:
        void setup( bool load_world_modfiles = true );
        /** Saving and loading functions. */
        void serialize( std::ostream &fout ); // for save
        void unserialize( std::istream &fin ); // for load
        void unserialize_master( std::istream &fin ); // for load

        /** write statistics to stdout and @return true if successful */
        bool dump_stats( const std::string &what, dump_mode mode, const std::vector<std::string> &opts );

        /** Returns false if saving failed. */
        bool save( bool quitting );

        /** Returns a list of currently active character saves. */
        std::vector<std::string> list_active_saves();
        void write_memorial_file( const std::string &filename, std::string sLastWords );
        bool cleanup_at_end();
        void start_calendar();
        /** MAIN GAME LOOP. Returns true if game is over (death, saved, quit, etc.). */
        bool do_turn();
        shared_ptr_fast<ui_adaptor> create_or_get_main_ui_adaptor();
        void invalidate_main_ui_adaptor() const;
        void mark_main_ui_adaptor_resize() const;
        void draw( ui_adaptor &ui );
        void draw_ter( bool draw_sounds = true );
        void draw_ter( const tripoint &center, bool looking = false, bool draw_sounds = true );

        class draw_callback_t
        {
            public:
                draw_callback_t( const std::function<void()> &cb );
                ~draw_callback_t();
                void operator()();
                friend class game;
            private:
                std::function<void()> cb;
                bool added = false;
        };
        /* Add callback that would be called in `game::draw`. This can be used to
         * implement map overlays in game menus. If parameters of the callback changes
         * during its lifetime, `invaliate_main_ui_adaptor` has to be called for
         * the changes to take effect immediately on the next call to `ui_manager::redraw`.
         * Otherwise the callback may not take effect until the main ui is invalidated
         * due to resizing or other menus closing. The callback is disabled once all
         * shared pointers to the callback are deconstructed, and is removed afterwards. */
        void add_draw_callback( const shared_ptr_fast<draw_callback_t> &cb );
    private:
        bool is_looking = false;
        std::vector<weak_ptr_fast<draw_callback_t>> draw_callbacks;

    public:
        // when force_redraw is true, redraw all panel instead of just animated panels
        // mostly used after UI updates
        void draw_panels( bool force_draw = false );
        /**
         * Returns the location where the indicator should go relative to the reality bubble,
         * or nothing to indicate no indicator should be drawn.
         * Based on the vehicle the player is driving, if any.
         * @param next If true, bases it on the vehicle the vehicle will turn to next turn,
         * instead of the one it is currently facing.
         */
        std::optional<tripoint> get_veh_dir_indicator_location( bool next ) const;
        void draw_veh_dir_indicator( bool next );

        /**
         * Moves the player vertically.
         * If force == true then they are falling.
         * If peeking == true, forbids some exotic movement options
         */
        void vertical_move( int z, bool force, bool peeking = false );
        void start_hauling( const tripoint &pos );
        /** Returns the other end of the stairs (if any). May query, affect u etc.  */
        std::optional<tripoint> find_or_make_stairs( map &mp, int z_after, bool &rope_ladder,
                bool peeking );
        /** Actual z-level movement part of vertical_move. Doesn't include stair finding, traps etc. */
        void vertical_shift( int z_after );
        /** Add goes up/down auto_notes (if turned on) */
        void vertical_notes( int z_before, int z_after );
        /** Checks to see if a player can use a computer (not illiterate, etc.) and uses if able. */
        void use_computer( const tripoint &p );
        /**
         * @return The living creature with the given id. Returns null if no living
         * creature with such an id exists. Never returns a dead creature.
         * Currently only the player character and npcs have ids.
         */
        template<typename T = Creature>
        T * critter_by_id( const character_id &id );
        /**
         * Returns the Creature at the given location. Optionally casted to the given
         * type of creature: @ref npc, @ref player, @ref monster - if there is a creature,
         * but it's not of the requested type, returns nullptr.
         * @param allow_hallucination Whether to return monsters that are actually hallucinations.
         */
        template<typename T = Creature>
        T * critter_at( const tripoint &p, bool allow_hallucination = false );
        template<typename T = Creature>
        const T * critter_at( const tripoint &p, bool allow_hallucination = false ) const;
        /**
        * Returns a shared pointer to the given critter (which can be of any of the subclasses of
        * @ref Creature). The function may return an empty pointer if the given critter
        * is not stored anywhere (e.g. it was allocated on the stack, not stored in
        * the @ref critter_tracker nor in @ref active_npc nor is it @ref u).
        */
        template<typename T = Creature>
        shared_ptr_fast<T> shared_from( const T &critter );

        /**
         * Adds critters to the reality bubble, creating them if necessary.
         * Functions taking a @p id parameter will construct a monster based on that id,
         * (with default properties).
         * Functions taking a @p mon parameter will use the supplied monster instance instead
         * (which must not be null).
         * Note: the monster will not be upgraded by these functions, it is placed as is.
         *
         * @ref place_critter_at will place the creature exactly at the given point.
         *
         * @ref place_critter_around will place the creature around
         * the center @p p within the given @p radius (radius 0 means only the center point is used).
         * The chosen point will be as close to the center as possible.
         *
         * @ref place_critter_within will place the creature at a random point within
         * that given range. (All points within have equal probability.)
         *
         * @return All functions return null if the creature could not be placed (usually because
         * the target is not suitable for it: may be a solid wall, or air, or already occupied
         * by some creature).
         * If the creature has been placed, it returns a pointer to it (which is the same as
         * the one contained in @p mon).
         */
        /** @{ */
        monster *place_critter_at( const mtype_id &id, const tripoint &p );
        monster *place_critter_at( const shared_ptr_fast<monster> &mon, const tripoint &p );
        monster *place_critter_around( const mtype_id &id, const tripoint &center, int radius );
        monster *place_critter_around( const shared_ptr_fast<monster> &mon, const tripoint &center,
                                       int radius, bool forced = false );
        monster *place_critter_within( const mtype_id &id, const tripoint_range<tripoint> &range );
        monster *place_critter_within( const shared_ptr_fast<monster> &mon,
                                       const tripoint_range<tripoint> &range );
        /** @} */
        /**
         * Returns the approximate number of creatures in the reality bubble.
         * Because of performance restrictions it may return a slightly incorrect
         * values (as it includes dead, but not yet cleaned up creatures).
         */
        size_t num_creatures() const;
        /** Redirects to the creature_tracker update_pos() function. */
        bool update_zombie_pos( const monster &critter, const tripoint &pos );
        void remove_zombie( const monster &critter );
        /** Redirects to the creature_tracker clear() function. */
        void clear_zombies();
        /** Spawns a hallucination at a determined position. */
        bool spawn_hallucination( const tripoint &p );
        /** Swaps positions of two creatures */
        bool swap_critters( Creature &, Creature & );

    private:
        friend class monster_range;
        friend class Creature_range;

        template<typename T>
        class non_dead_range
        {
            public:
                std::vector<weak_ptr_fast<T>> items;

                class iterator
                {
                    private:
                        bool valid();
                    public:
                        std::vector<weak_ptr_fast<T>> &items;
                        typename std::vector<weak_ptr_fast<T>>::iterator iter;
                        shared_ptr_fast<T> current;

                        iterator( std::vector<weak_ptr_fast<T>> &i,
                                  const typename std::vector<weak_ptr_fast<T>>::iterator t ) : items( i ), iter( t ) {
                            while( iter != items.end() && !valid() ) {
                                ++iter;
                            }
                        }
                        iterator( const iterator & ) = default;
                        iterator &operator=( const iterator & ) = default;

                        bool operator==( const iterator &rhs ) const {
                            return iter == rhs.iter;
                        }
                        bool operator!=( const iterator &rhs ) const {
                            return !operator==( rhs );
                        }
                        iterator &operator++() {
                            do {
                                ++iter;
                            } while( iter != items.end() && !valid() );
                            return *this;
                        }
                        T &operator*() const {
                            return *current;
                        }
                };
                iterator begin() {
                    return iterator( items, items.begin() );
                }
                iterator end() {
                    return iterator( items, items.end() );
                }
        };

        class monster_range : public non_dead_range<monster>
        {
            public:
                monster_range( game &game_ref );
        };

        class npc_range : public non_dead_range<npc>
        {
            public:
                npc_range( game &game_ref );
        };

        class Creature_range : public non_dead_range<Creature>
        {
            private:
                shared_ptr_fast<player> u;

            public:
                Creature_range( game &game_ref );
        };

    public:
        /**
         * Returns an anonymous range that contains all creatures. The range allows iteration
         * via a range-based for loop, e.g. `for( Creature &critter : all_creatures() ) { ... }`.
         * One shall not store the returned range nor the iterators.
         * One can freely remove and add creatures to the game during the iteration. Added
         * creatures will not be iterated over.
         */
        Creature_range all_creatures();
        /// Same as @ref all_creatures but iterators only over monsters.
        monster_range all_monsters();
        /// Same as @ref all_creatures but iterators only over npcs.
        npc_range all_npcs();

        /**
         * Returns all creatures matching a predicate. Only living ( not dead ) creatures
         * are checked ( and returned ). Returned pointers are never null.
         */
        std::vector<Creature *> get_creatures_if( const std::function<bool( const Creature & )> &pred );
        std::vector<npc *> get_npcs_if( const std::function<bool( const npc & )> &pred );
        /**
         * Returns a creature matching a predicate. Only living (not dead) creatures
         * are checked. Returns `nullptr` if no creature matches the predicate.
         * There is no guarantee which creature is returned when several creatures match.
         */
        Creature *get_creature_if( const std::function<bool( const Creature & )> &pred );

        /** Returns true if there is no player, NPC, or monster on the tile and move_cost > 0. */
        bool is_empty( const tripoint &p );
        /** Returns true if p is outdoors and it is sunny. */
        bool is_in_sunlight( const tripoint &p );
        /** Returns true if p is indoors, underground, or in a car. */
        bool is_sheltered( const tripoint &p );
        /**
         * Revives a corpse at given location. The monster type and some of its properties are
         * deducted from the corpse. If reviving succeeds, the location is guaranteed to have a
         * new monster there (see @ref critter_at).
         * @param p The place where to put the revived monster.
         * @param it The corpse item, it must be a valid corpse (see @ref item::is_corpse).
         * @return Whether the corpse has actually been redivided. Reviving may fail for many
         * reasons, including no space to put the monster, corpse being to much damaged etc.
         * If the monster was revived, the caller should remove the corpse item.
         * If reviving failed, the item is unchanged, as is the environment (no new monsters).
         */
        bool revive_corpse( const tripoint &p, item &it );
        /**Turns Broken Cyborg monster into Cyborg NPC via surgery*/
        void save_cyborg( item *cyborg, const tripoint &couch_pos, Character &installer );
        /** Asks if the player wants to cancel their activity, and if so cancels it. */
        bool cancel_activity_query( const std::string &text );
        /** Asks if the player wants to cancel their activity and if so cancels it. Additionally checks
         *  if the player wants to ignore further distractions. */
        bool cancel_activity_or_ignore_query( distraction_type type, const std::string &text );
        /** Handles players exiting from moving vehicles. */
        void moving_vehicle_dismount( const tripoint &dest_loc );

        /** Returns the current remotely controlled vehicle. */
        vehicle *remoteveh();
        /** Sets the current remotely controlled vehicle. */
        void setremoteveh( vehicle *veh );

        /** Returns the next available mission id. */
        int assign_mission_id();
        /** Find the npc with the given ID. Returns NULL if the npc could not be found. Searches all loaded overmaps. */
        npc *find_npc( character_id id );
        /** Makes any nearby NPCs on the overmap active. */
        void load_npcs();
    private:
        /** Unloads all NPCs.
         *
         * If you call this you must later call load_npcs, lest caches get
         * rather confused.  The tests used to call this a lot when they
         * shouldn't. It is now private to reduce the chance of similar
         * problems in the future.
         */
        void unload_npcs();
    public:
        /** Unloads, then loads the NPCs */
        void reload_npcs();
        /** Add follower id to set of followers. */
        void add_npc_follower( const character_id &id );
        /** Remove follower id from follower set. */
        void remove_npc_follower( const character_id &id );
        /** Get set of followers. */
        std::set<character_id> get_follower_list();
        /** validate list of followers to account for overmap buffers */
        void validate_npc_followers();
        void validate_mounted_npcs();
        /** validate towed vehicles so they get linked up again after a load */
        void validate_linked_vehicles();
        /** process vehicles that are following the player */
        void autopilot_vehicles();
        /** Picks and spawns a random fish from the remaining fish list when a fish is caught. */
        void catch_a_monster( monster *fish, const tripoint &pos, Character *who,
                              const time_duration &catch_duration );
        /**
         * Get the contiguous fishable locations starting at fish_pos, out to the specificed distance.
         * @param distance Distance around the fish_pos to examine for contiguous fishable locations.
         * @param fish_pos The location being fished.
         * @return A set of locations representing the valid contiguous fishable locations.
         */
        std::unordered_set<tripoint> get_fishable_locations( int distance, const tripoint &fish_pos );
        /**
         * Get the fishable monsters within the provided fishable locations.
         * @param fishable_locations A set of locations which are valid fishable terrain. Any fishable monsters
         * are filtered by this collection to determine those which can actually be caught.
         * @return Fishable monsters within the specified fishable terrain.
         */
        std::vector<monster *> get_fishable_monsters( std::unordered_set<tripoint> &fishable_locations );

        /** Flings the input creature in the given direction. */
        void fling_creature( Creature *c, const units::angle &dir, float flvel, bool controlled = false );

        float natural_light_level( int zlev ) const;
        /** Returns coarse number-of-squares of visibility at the current light level.
         * Used by monster and NPC AI.
         */
        unsigned char light_level( int zlev ) const;
        void reset_light_level();
        character_id assign_npc_id();
        Creature *is_hostile_nearby();
        Creature *is_hostile_very_close();
        // Handles shifting coordinates transparently when moving between submaps.
        // Helper to make calling with a player pointer less verbose.
        point update_map( Character &who );
        point update_map( int &x, int &y );
        void update_overmap_seen(); // Update which overmap tiles we can see

        void process_artifact( item &it, Character &who );
        void add_artifact_messages( const std::vector<art_effect_passive> &effects );
        void add_artifact_dreams( );

        void peek();
        void peek( const tripoint &p );
        std::optional<tripoint> look_debug();

        bool check_zone( const zone_type_id &type, const tripoint &where ) const;
        /** Checks whether or not there is a zone of particular type nearby */
        bool check_near_zone( const zone_type_id &type, const tripoint &where ) const;
        bool is_zones_manager_open() const;
        void zones_manager();

        // Look at nearby terrain ';', or select zone points
        std::optional<tripoint> look_around( bool force_3d = false );
        /**
         * @brief
         *
         * @param show_window display the info window that holds the tile information in the position.
         * @param center used to calculate the u.view_offset, could center the screen to the position it represents
         * @param start_point  the start point of the targeting zone, also the initial local position of the cursor
         * @param has_first_point should be true if the first point has been selected when editing the zone
         * @param select_zone true if the zone is being edited
         * @param peeking determines if the player is peeking
         * @param is_moving_zone true if the zone is being moved, false by default
         * @param end_point the end point of the targeting zone, only used if is_moving_zone is true, default is tripoint_zero
         * @return look_around_result
         */
        look_around_result look_around( bool show_window, tripoint &center,
                                        const tripoint &start_point, bool has_first_point, bool select_zone, bool peeking,
                                        bool is_moving_zone = false, const tripoint &end_point = tripoint_zero, bool force_3d = false );

        // Shared method to print "look around" info
        void pre_print_all_tile_info( const tripoint &lp, const catacurses::window &w_info,
                                      int &line, int last_line, const visibility_variables &cache );

        // Shared method to print "look around" info
        void print_all_tile_info( const tripoint &lp, const catacurses::window &w_look,
                                  const std::string &area_name, int column,
                                  int &line, int last_line, const visibility_variables &cache );

        void draw_look_around_cursor( const tripoint &lp, const visibility_variables &cache );

        /** Long description of (visible) things at tile. */
        void extended_description( const tripoint &p );

        void draw_trail_to_square( const tripoint &t, bool bDrawX );

        /** Custom-filtered menu for inventory and nearby items and those that within specified radius */
        item *inv_map_splice( const item_filter &filter, const std::string &title, int radius = 0,
                              const std::string &none_message = "" );

        bool has_gametype() const;
        special_game_type gametype() const;

        void toggle_fullscreen();
        void toggle_pixel_minimap();
        void reload_tileset( const std::function<void( std::string )> &out );
        void temp_exit_fullscreen();
        void reenter_fullscreen();
        void zoom_in();
        void zoom_out();
        void reset_zoom();
        void set_zoom( float level );
        float get_zoom() const;
        int get_moves_since_last_save() const;
        int get_user_action_counter() const;

        /** Saves a screenshot of the current viewport, as a PNG file, to the given location.
        * @param file_path: A full path to the file where the screenshot should be saved.
        * @note: Only works for SDL/TILES (otherwise the function returns `false`). A window (more precisely, a viewport) must already exist and the SDL renderer must be valid.
        * @returns `true` if the screenshot generation was successful, `false` otherwise.
        */
        bool take_screenshot( const std::string &file_path ) const;
        /** Saves a screenshot of the current viewport, as a PNG file. Filesystem location is derived from the current world and character.
        * @note: Only works for SDL/TILES (otherwise the function returns `false`). A window (more precisely, a viewport) must already exist and the SDL renderer must be valid.
        * @returns `true` if the screenshot generation was successful, `false` otherwise.
        */
        bool take_screenshot() const;

        /**
         * The top left corner of the reality bubble (in submaps coordinates). This is the same
         * as @ref map::abs_sub of the @ref m map.
         */
        int get_levx() const;
        int get_levy() const;
        int get_levz() const;
        /**
         * Load the main map at given location, see @ref map::load, in global, absolute submap
         * coordinates.
         * @param pump_events If true, handle window events during loading. If
         * you set this to true, do ensure that the map is not accessed before
         * this function returns (for example, UIs that draw the map should be
         * disabled).
         */
        void load_map( const tripoint &pos_sm, bool pump_events = false );
        void load_map( const tripoint_abs_sm &pos_sm, bool pump_events = false );
        /**
         * The overmap which contains the center submap of the reality bubble.
         */
        overmap &get_cur_om() const;

        /** Get all living player allies */
        std::vector<npc *> allies();
        // Setter for driving_view_offset
        void set_driving_view_offset( point p );
        // Calculates the driving_view_offset for the given vehicle
        // and sets it (view set_driving_view_offset), if
        // the options for this feature is deactivated or if veh is NULL,
        // the function set the driving offset to (0,0)
        void calc_driving_offset( vehicle *veh = nullptr );

        /**@}*/

        void toggle_gate( const tripoint &p );

        // Knockback functions: knock target at t along a line, either calculated
        // from source position s using force parameter or passed as an argument;
        // force determines how far target is knocked, if trajectory is calculated
        // force also determines damage along with dam_mult;
        // stun determines base number of turns target is stunned regardless of impact
        // stun == 0 means no stun, stun == -1 indicates only impact stun (wall or npc/monster)
        void knockback( const tripoint &s, const tripoint &t, int force, int stun, int dam_mult,
                        Creature *source );
        void knockback( std::vector<tripoint> &traj, int stun, int dam_mult, Creature *source );

        // Animation related functions
        void draw_bullet( const tripoint &t, int i, const std::vector<tripoint> &trajectory,
                          char bullet );
        void draw_hit_mon( const tripoint &p, const monster &m, bool dead = false );
        void draw_hit_player( const Character &p, int dam );
        void draw_line( const tripoint &p, const tripoint &center_point,
                        const std::vector<tripoint> &points, bool noreveal = false );
        void draw_line( const tripoint &p, const std::vector<tripoint> &points );
        void draw_weather( const weather_printable &wPrint );
        void draw_sct();
        void draw_zones( const tripoint &start, const tripoint &end, const tripoint &offset );
        // In curses mode, draw critter (if visible!) on its current position into w_terrain.
        // @param center the center of view, same as when calling map::draw
        void draw_critter( const Creature &critter, const tripoint &center );
        // As @ref draw_critter, but with inverted colors.
        void draw_critter_highlighted( const Creature &critter, const tripoint &center );
        void draw_cursor( const tripoint &p );
        // Draw a highlight graphic at p, for example when examining something.
        // TILES only, in curses this does nothing
        void draw_highlight( const tripoint &p );
        void draw_radiation_override( const tripoint &p, int rad );
        void draw_terrain_override( const tripoint &p, const ter_id &id );
        void draw_furniture_override( const tripoint &p, const furn_id &id );
        void draw_graffiti_override( const tripoint &p, bool has );
        void draw_trap_override( const tripoint &p, const trap_id &id );
        void draw_field_override( const tripoint &p, const field_type_id &id );
        void draw_item_override( const tripoint &p, const itype_id &id, const mtype_id &mid,
                                 bool hilite );
        void draw_vpart_override( const tripoint &p, const vpart_id &id, int part_mod,
                                  units::angle veh_dir, bool hilite, point mount );
        void draw_below_override( const tripoint &p, bool draw );
        void draw_monster_override( const tripoint &p, const mtype_id &id, int count,
                                    bool more, Attitude att );

        bool is_in_viewport( const tripoint &p, int margin = 0 ) const;
        /**
         * Check whether movement is allowed according to safe mode settings.
         * @return true if the movement is allowed, otherwise false.
         */
        bool check_safe_mode_allowed( bool repeat_safe_mode_warnings = true );
        void set_safe_mode( safe_mode_type mode );

        /** open vehicle interaction screen */
        void exam_vehicle( vehicle &veh, point cp = point_zero );

        // Forcefully close a door at p.
        // The function checks for creatures/items/vehicles at that point and
        // might kill/harm/destroy them.
        // If there still remains something that prevents the door from closing
        // (e.g. a very big creatures, a vehicle) the door will not be closed and
        // the function returns false.
        // If the door gets closed the terrain at p is set to door_type and
        // true is returned.
        // bash_dmg controls how much damage the door does to the
        // creatures/items/vehicle.
        // If bash_dmg is 0 or smaller, creatures and vehicles are not damaged
        // at all and they will prevent the door from closing.
        // If bash_dmg is smaller than 0, _every_ item on the door tile will
        // prevent the door from closing. If bash_dmg is 0, only very small items
        // will do so, if bash_dmg is greater than 0, items won't stop the door
        // from closing at all.
        // If the door gets closed the items on the door tile get moved away or destroyed.
        bool forced_door_closing( const tripoint &p, const ter_id &door_type, int bash_dmg );

        /** Attempt to load first valid save (if any) in world */
        bool load( const std::string &world );

        /** Returns true if the menu handled stuff and player shouldn't do anything else */
        bool npc_menu( npc &who );

        // Handle phasing through walls, returns true if it handled the move
        bool phasing_move( const tripoint &dest, bool via_ramp = false );
        // Regular movement. Returns false if it failed for any reason
        bool walk_move( const tripoint &dest, bool via_ramp = false );
        void on_move_effects();
    private:
        // Game-start procedures
        bool load( const save_t &name ); // Load a player-specific save file
        void load_master(); // Load the master data file, with factions &c
#if defined(__ANDROID__)
        void load_shortcuts( std::istream &fin );
#endif
        bool start_game(); // Starts a new game in the active world

        //private save functions.
        // returns false if saving failed for whatever reason
        bool save_factions_missions_npcs();
        void reset_npc_dispositions();
        void serialize_master( std::ostream &fout );
        // returns false if saving failed for whatever reason
        bool save_artifacts();
        // returns false if saving failed for whatever reason
        bool save_maps();
#if defined(__ANDROID__)
        void save_shortcuts( std::ostream &fout );
#endif
        // Data Initialization
        void init_autosave();     // Initializes autosave parameters
        void create_starting_npcs(); // Creates NPCs that start near you
        // create vehicle nearby, for example; for a profession vehicle.
        vehicle *place_vehicle_nearby(
            const vproto_id &id, const point_abs_omt &origin, int min_distance,
            int max_distance, const std::vector<std::string> &omt_search_types = {} );
        // V Menu Functions and helpers:
        void list_items_monsters(); // Called when you invoke the `V`-menu

        enum class vmenu_ret : int {
            CHANGE_TAB,
            QUIT,
            FIRE, // Who knew, apparently you can do that in list_monsters
        };

        game::vmenu_ret list_items( const std::vector<map_item_stack> &item_list );
        std::vector<map_item_stack> find_nearby_items( int iRadius );
        void reset_item_list_state( const catacurses::window &window, int height, bool bRadiusSort );

        game::vmenu_ret list_monsters( const std::vector<Creature *> &monster_list );

        /** Check for dangerous stuff at dest_loc, return false if the player decides
        not to step there */
        // Handle pushing during move, returns true if it handled the move
        bool grabbed_move( const tripoint &dp );
        bool grabbed_veh_move( const tripoint &dp );
        bool grabbed_furn_move( const tripoint &dp );

        void control_vehicle(); // Use vehicle controls  '^'
        void examine( const tripoint &p ); // Examine nearby terrain  'e'
        void examine();

        void pickup(); // Pickup nearby items 'g', min 0
        void pickup( const tripoint &p );
        void pickup_feet(); // Pick items at player position ',', min 1

        void drop(); // Drop an item  'd'
        void drop_in_direction(); // Drop w/ direction  'D'

        void butcher(); // Butcher a corpse  'B'
    public:
        // Places the player at the specified point; hurts feet, lists items etc.
        point place_player( const tripoint &dest );
        void place_player_overmap( const tripoint_abs_omt &om_dest );

        unsigned int get_seed() const;

        /** If invoked, NPCs will be reloaded before next turn. */
        void set_npcs_dirty();
        /** If invoked, dead will be cleaned this turn. */
        void set_critter_died();
        void mon_info( const catacurses::window &,
                       int hor_padding = 0 ); // Prints a list of nearby monsters
        void mon_info_update( );    //Update seen monsters information
        void cleanup_dead();     // Delete any dead NPCs/monsters
        bool is_dangerous_tile( const tripoint &dest_loc ) const;
        std::vector<std::string> get_dangerous_tile( const tripoint &dest_loc ) const;
        bool prompt_dangerous_tile( const tripoint &dest_loc ) const;
    private:
        void chat(); // Talk to a nearby NPC  'C'

        // Internal methods to show "look around" info
        void print_fields_info( const tripoint &lp, const catacurses::window &w_look, int column,
                                int &line );
        void print_terrain_info( const tripoint &lp, const catacurses::window &w_look,
                                 const std::string &area_name, int column,
                                 int &line );
        void print_trap_info( const tripoint &lp, const catacurses::window &w_look, int column,
                              int &line );
        void print_creature_info( const Creature *creature, const catacurses::window &w_look, int column,
                                  int &line, int last_line );
        void print_vehicle_info( const vehicle *veh, int veh_part, const catacurses::window &w_look,
                                 int column, int &line, int last_line );
        void print_visibility_info( const catacurses::window &w_look, int column, int &line,
                                    visibility_type visibility );
        void print_items_info( const tripoint &lp, const catacurses::window &w_look, int column, int &line,
                               int last_line );
        void print_graffiti_info( const tripoint &lp, const catacurses::window &w_look, int column,
                                  int &line, int last_line );

        input_context get_player_input( std::string &action );

        // Map updating and monster spawning
        void replace_stair_monsters();
        void update_stair_monsters();
        /**
         * Shift all active monsters, the shift vector is the number of
         * shifted submaps. Monsters that are outside of the reality bubble after
         * shifting are despawned.
         * Note on z-levels: this works with vertical shifts, but currently all
         * monsters are despawned upon a vertical shift.
         */
        void shift_monsters( const tripoint &shift );
    public:
        /**
         * Despawn a specific monster, it's stored on the overmap. Also removes
         * it from the creature tracker. Keep in mind that any monster index may
         * point to a different monster after calling this (or to no monster at all).
         */
        void despawn_monster( monster &critter );

        /**
         * Marks the game as won. Doesn't end the game.
         */
        void win();

    private:
        void perhaps_add_random_npc();

        // Routine loop functions, approximately in order of execution
        void monmove();          // Monster movement
        void overmap_npc_move(); // NPC overmap movement
        void process_voluntary_act_interrupt(); // Process
        void process_activity(); // Processes and enacts the player's activity
        void handle_key_blocking_activity(); // Abort reading etc.
        void open_consume_item_menu(); // Custom menu for consuming specific group of items
        bool handle_action();
        bool try_get_right_click_action( action_id &act, const tripoint &mouse_target );
        bool try_get_left_click_action( action_id &act, const tripoint &mouse_target );

        void item_action_menu(); // Displays item action menu

        bool is_game_over();     // Returns true if the player quit or died
        void death_screen();     // Display our stats, "GAME OVER BOO HOO"
        void win_screen();       // Display our stats, "CONGRATULATIONS!"
        void draw_minimap();     // Draw the 5x5 minimap
    public:
        // Draws the pixel minimap based on the player's current location
        void draw_pixel_minimap( const catacurses::window &w );
    private:

        //  int autosave_timeout();  // If autosave enabled, how long we should wait for user inaction before saving.
        void autosave();         // automatic quicksaves - Performs some checks before calling quicksave()
    public:
        void quicksave();        // Saves the game without quitting
        void disp_NPCs();        // Currently for debug use.  Lists global NPCs.

        void list_missions();       // Listed current, completed and failed missions (mission_ui.cpp)
    private:
        void quickload();        // Loads the previously saved game if it exists

        // Input related
        // Handles box showing items under mouse
        bool handle_mouseview( input_context &ctxt, std::string &action );

        // On-request draw functions
        void display_faction_epilogues();
        void disp_NPC_epilogues();  // Display NPC endings

        /* Debug functions */
        // currently displayed overlay (none is displayed if empty)
        std::optional<action_id> displaying_overlays;
        void display_scent();   // Displays the scent map
        void display_temperature();    // Displays temperature map
        void display_vehicle_ai(); // Displays vehicle autopilot AI overlay
        void display_visibility(); // Displays visibility map
        void display_lighting(); // Displays lighting conditions heat map
        void display_radiation(); // Displays radiation map
        void display_transparency(); // Displays transparency map

        // prints the IRL time in ms of the last full in-game hour
        class debug_hour_timer
        {
            public:
                using IRLTimeMs = std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds>;
                void toggle();
                void print_time();
            private:
                bool enabled = false;
                std::optional<IRLTimeMs> start_time = std::nullopt;
        } debug_hour_timer;

        Creature *is_hostile_within( int distance );

        void move_save_to_graveyard( const std::string &dirname );
        bool save_player_data();
        bool save_uistate_data() const;
        // ########################## DATA ################################
    private:
        // May be a bit hacky, but it's probably better than the header spaghetti
        pimpl<map> map_ptr;
        pimpl<avatar> u_ptr;
        pimpl<live_view> liveview_ptr;
        live_view &liveview;
        pimpl<scent_map> scent_ptr;
        pimpl<timed_event_manager> timed_event_manager_ptr;
        pimpl<event_bus> event_bus_ptr;
        pimpl<stats_tracker> stats_tracker_ptr;
        pimpl<kill_tracker> kill_tracker_ptr;
        pimpl<achievements_tracker> achievements_tracker_ptr;
        pimpl<memorial_logger> memorial_logger_ptr;
        pimpl<spell_events> spell_events_ptr;
        pimpl<distribution_grid_tracker> grid_tracker_ptr;
        pimpl<weather_manager> weather_manager_ptr;

    public:
        map &m;
        avatar &u;
        scent_map &scent;
        timed_event_manager &timed_events;

        event_bus &events();
        stats_tracker &stats();
        kill_tracker &get_kill_tracker();
        memorial_logger &memorial();
        spell_events &spell_events_subscriber();

        pimpl<Creature_tracker> critter_tracker;
        pimpl<faction_manager> faction_manager_ptr;
        pimpl<drop_token_provider> token_provider_ptr;

        /** Used in main.cpp to determine what type of quit is being performed. */
        quit_status uquit;
        /** True if the game has just started or loaded, else false. */
        bool new_game = false;

        const scenario *scen = nullptr;
        std::vector<shared_ptr_fast<monster>> coming_to_stairs;
        int monstairz = 0;

        tripoint ter_view_p;
        catacurses::window w_terrain;
        catacurses::window w_overmap;
        catacurses::window w_omlegend;
        catacurses::window w_minimap;
        catacurses::window w_pixel_minimap;
        //only a pointer, can refer to w_messages_short or w_messages_long

        // View offset based on the driving speed (if any)
        // that has been added to u.view_offset,
        // Don't write to this directly, always use set_driving_view_offset
        point driving_view_offset;

        bool debug_pathfinding = false; // show NPC pathfinding on overmap ui
        bool debug_submap_grid_overlay = false;

        /* tile overlays */
        // Toggle all other overlays off and flip the given overlay on/off.
        void display_toggle_overlay( action_id );
        // Get the state of an overlay (on/off).
        bool display_overlay_state( action_id );
        // toggles the timing of in-game hours
        void toggle_debug_hour_timer();
        /** Creature for which to display the visibility map */
        Creature *displaying_visibility_creature;
        /** Type of lighting condition overlay to display */
        int displaying_lighting_condition = 0;

        bool show_panel_adm = false;
        bool right_sidebar = false;
        bool fullscreen = false;
        bool was_fullscreen = false;
        bool auto_travel_mode = false;
        bool queue_screenshot = false;
        safe_mode_type safe_mode;
        int turnssincelastmon = 0; // needed for auto run mode

        int mostseen = 0; // # of mons seen last turn; if this increases, set safe_mode to SAFE_MODE_STOP
    private:
        shared_ptr_fast<player> u_shared_ptr;

        catacurses::window w_terrain_ptr;
        catacurses::window w_minimap_ptr;

        std::string sFilter; // a member so that it's remembered over time
        std::string list_item_upvote;
        std::string list_item_downvote;

        bool safe_mode_warning_logged = false;
        bool bVMonsterLookFire = false;
        character_id next_npc_id;
        std::list<shared_ptr_fast<npc>> active_npc;
        int next_mission_id = 0;
        std::set<character_id> follower_ids; // Keep track of follower NPC IDs
        int moves_since_last_save = 0;
        time_t last_save_timestamp;
        mutable std::array<float, OVERMAP_LAYERS> latest_lightlevels;
        // remoteveh() cache
        time_point remoteveh_cache_time;
        vehicle *remoteveh_cache;
        /** Has a NPC been spawned since last load? */
        bool npcs_dirty = false;
        /** Has anything died in this turn and needs to be cleaned up? */
        bool critter_died = false;
        /** Is this the first redraw since waiting (sleeping or activity) started */
        bool first_redraw_since_waiting_started = true;
        /** Is Zone manager open or not - changes graphics of some zone tiles */
        bool zones_manager_open = false;

        std::unique_ptr<special_game> gamemode;

        int user_action_counter = 0; // Times the user has input an action

        /** How far the tileset should be zoomed out, 16 is default. 32 is zoomed in by x2, 8 is zoomed out by x0.5 */
        float tileset_zoom = 0;

        /** Seed for all the random numbers that should have consistent randomness (weather). */
        unsigned int seed = 0;

        // Preview for auto move route
        std::vector<tripoint> destination_preview;

        std::chrono::time_point<std::chrono::steady_clock> last_mouse_edge_scroll;
        tripoint last_mouse_edge_scroll_vector_terrain;
        tripoint last_mouse_edge_scroll_vector_overmap;
        std::pair<tripoint, tripoint> mouse_edge_scrolling( input_context &ctxt, int speed,
                const tripoint &last, bool iso );

        weak_ptr_fast<ui_adaptor> main_ui_adaptor;

        std::unique_ptr<static_popup> wait_popup;
    public:
        /** Used to implement mouse "edge scrolling". Returns a
         *  tripoint which is a vector of the resulting "move", i.e.
         *  (0, 0, 0) if the mouse is not at the edge of the screen,
         *  otherwise some (x, y, 0) depending on which edges are
         *  hit.
         *  This variant adjust scrolling speed according to zoom
         *  level, making it suitable when viewing the "terrain".
         */
        tripoint mouse_edge_scrolling_terrain( input_context &ctxt );
        /** This variant is suitable for the overmap. */
        tripoint mouse_edge_scrolling_overmap( input_context &ctxt );

        // called on map shifting
        void shift_destination_preview( point delta );

        /**
        Checks if player is able to successfully climb to/from some terrain and not slip down
        @return whether player has slipped down
        */
        bool slip_down();
};

// Returns temperature modifier from direct heat radiation of nearby sources
// @param location Location affected by heat sources
// @param direct forces return of heat intensity (and not temperature modifier) of
// adjacent hottest heat source
int get_heat_radiation( const tripoint &location, bool direct );
// Returns temperature modifier from hot air fields of given location
int get_convection_temperature( const tripoint &location );

namespace cata_event_dispatch
{
// Constructs and dispatches an avatar movement event with the necessary parameters
// @param u The avatar moving
// @param m The map the avatar is moving on
// @param p The point the avatar is moving to on map m
void avatar_moves( const avatar &u, const map &m, const tripoint &p );
} // namespace cata_event_dispatch


