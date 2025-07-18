#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "animation.h"
#include "enums.h"
#include "lightmap.h"
#include "line.h"
#include "map_memory.h"
#include "options.h"
#include "pimpl.h"
#include "point.h"
#include "sdl_wrappers.h"
#include "sdl_geometry.h"
#include "type_id.h"
#include "weather.h"
#include "weighted_list.h"

class Character;
class JsonObject;
class pixel_minimap;

extern void set_displaybuffer_rendertarget();

/** Structures */
struct tile_type {
    // fg and bg are both a weighted list of lists of sprite IDs
    weighted_int_list<std::vector<int>> fg, bg;
    bool multitile = false;
    bool rotates = false;
    bool animated = false;
    bool has_om_transparency = false;
    int height_3d = 0;
    point offset = point_zero;

    std::vector<std::string> available_subtiles;
};

// Make sure to change TILE_CATEGORY_IDS if this changes!
enum TILE_CATEGORY {
    C_NONE,
    C_VEHICLE_PART,
    C_TERRAIN,
    C_ITEM,
    C_FURNITURE,
    C_TRAP,
    C_FIELD,
    C_LIGHTING,
    C_MONSTER,
    C_BULLET,
    C_HIT_ENTITY,
    C_WEATHER,
    C_OVERMAP_TERRAIN,
    C_OVERMAP_NOTE
};

class tile_lookup_res
{
        // references are stored as pointers to support copy assignment of the class
        const std::string *_id;
        tile_type *_tile;
    public:
        tile_lookup_res( const std::string &id, tile_type &tile ): _id( &id ), _tile( &tile ) {}
        const std::string &id() {
            return *_id;
        }
        tile_type &tile() {
            return *_tile;
        }
};

class texture
{
    private:
        std::shared_ptr<SDL_Texture> sdl_texture_ptr;
        SDL_Rect srcrect = { 0, 0, 0, 0 };

    public:
        texture( std::shared_ptr<SDL_Texture> ptr, const SDL_Rect &rect ) : sdl_texture_ptr( ptr ),
            srcrect( rect ) { }
        texture() = default;

        /// Returns the width (first) and height (second) of the stored texture.
        std::pair<int, int> dimension() const {
            return std::make_pair( srcrect.w, srcrect.h );
        }
        /// Interface to @ref SDL_RenderCopyEx, using this as the texture, and
        /// null as source rectangle (render the whole texture). Other parameters
        /// are simply passed through.
        int render_copy_ex( const SDL_Renderer_Ptr &renderer, const SDL_Rect *const dstrect,
                            const double angle,
                            const SDL_Point *const center, const SDL_RendererFlip flip ) const {
            return SDL_RenderCopyEx( renderer.get(), sdl_texture_ptr.get(), &srcrect, dstrect, angle, center,
                                     flip );
        }

        int set_alpha_mod( int mod ) const {
            return SDL_SetTextureAlphaMod( sdl_texture_ptr.get(), mod );
        }
};

class tileset
{
    private:
        struct season_tile_value {
            tile_type *default_tile = nullptr;
            std::optional<tile_lookup_res> season_tile = std::nullopt;
        };

        std::string tileset_id;

        int tile_width;
        int tile_height;

        // multiplier for pixel-doubling tilesets
        float tile_pixelscale;

        std::vector<texture> tile_values;
        std::vector<texture> shadow_tile_values;
        std::vector<texture> night_tile_values;
        std::vector<texture> overexposed_tile_values;
        std::vector<texture> memory_tile_values;
        std::vector<texture> z_overlay_values;

        std::unordered_map<std::string, tile_type> tile_ids;
        // caches both "default" and "_season_XXX" tile variants (to reduce the number of lookups)
        // either variant can be either a `nullptr` or a pointer/reference to the real value (stored inside `tile_ids`)
        std::unordered_map<std::string, season_tile_value> tile_ids_by_season[season_type::NUM_SEASONS];

        static const texture *get_if_available( const size_t index,
                                                const decltype( shadow_tile_values ) &tiles ) {
            return index < tiles.size() ? & tiles[index] : nullptr;
        }

        friend class tileset_loader;

    public:
        int get_tile_width() const {
            return tile_width;
        }
        int get_tile_height() const {
            return tile_height;
        }
        float get_tile_pixelscale() const {
            return tile_pixelscale;
        }
        const std::string &get_tileset_id() const {
            return tileset_id;
        }

        const texture *get_tile( const size_t index ) const {
            return get_if_available( index, tile_values );
        }
        const texture *get_night_tile( const size_t index ) const {
            return get_if_available( index, night_tile_values );
        }
        const texture *get_shadow_tile( const size_t index ) const {
            return get_if_available( index, shadow_tile_values );
        }
        const texture *get_overexposed_tile( const size_t index ) const {
            return get_if_available( index, overexposed_tile_values );
        }
        const texture *get_memory_tile( const size_t index ) const {
            return get_if_available( index, memory_tile_values );
        }
        const texture *get_z_overlay( const size_t index ) const {
            return get_if_available( index, z_overlay_values );
        }

        tile_type &create_tile_type( const std::string &id, tile_type &&new_tile_type );
        const tile_type *find_tile_type( const std::string &id ) const;
        /**
         * Looks up tile by id + season suffix AND just raw id
         * Example: if id == "t_tree_apple" and season == SPRING
         *    will first look up "t_tree_apple_season_spring"
         *    if not found, will look up "t_tree_apple"
         *    if still nothing is found, will return std::nullopt
         * @param id : "raw" tile id (without season suffix)
         * @param season : season suffix encoded as season_type enum
         * @return std::nullopt if no tile is found,
         *    std::optional with found id (e.g. "t_tree_apple_season_spring" or "t_tree_apple) and found tile.
         *
         * Note: this method is guaranteed to return pointers to the keys and values stored inside the
         * `tileset::tile_ids` collection. I.e. result of this method call is invalidated when
         *  the corresponding `tileset` is invalidated.
         */
        std::optional<tile_lookup_res> find_tile_type_by_season( const std::string &id,
                season_type season ) const;
};

class tileset_loader
{
    private:
        tileset &ts;
        const SDL_Renderer_Ptr &renderer;

        point sprite_offset;

        int sprite_width = 0;
        int sprite_height = 0;

        int offset = 0;
        int sprite_id_offset = 0;
        int size = 0;

        int R = 0;
        int G = 0;
        int B = 0;

        int tile_atlas_width = 0;

        void ensure_default_item_highlight();

        /** Returns false if failed to create texture. */
        bool copy_surface_to_texture( const SDL_Surface_Ptr &surf, point offset,
                                      std::vector<texture> &target );

        /** Returns false if failed to create texture(s). */
        bool create_textures_from_tile_atlas( const SDL_Surface_Ptr &tile_atlas, point offset );

        void process_variations_after_loading( weighted_int_list<std::vector<int>> &v );

        void add_ascii_subtile( tile_type &curr_tile, const std::string &t_id, int sprite_id,
                                const std::string &s_id );
        void load_ascii_set( const JsonObject &entry );

        tile_type &load_tile( const JsonObject &entry, const std::string &id );

        void load_tile_spritelists( const JsonObject &entry, weighted_int_list<std::vector<int>> &vs,
                                    const std::string &objname );

        void load_ascii( const JsonObject &config );
        /** Load tileset, R,G,B, are the color components of the transparent color
         * Returns the number of tiles that have been loaded from this tileset image
         * @param pump_events Handle window events and refresh the screen when necessary.
         *        Please ensure that the tileset is not accessed when this method is
         *        executing if you set it to true.
         * @throw std::exception If the image can not be loaded.
         */
        void load_tileset( const std::string &path, bool pump_events );
        /**
         * Load tiles from json data.This expects a "tiles" array in
         * <B>config</B>. That array should contain all the tile definition that
         * should be taken from an tileset image.
         * Because the function only loads tile definitions for a single tileset
         * image, only tile indices (tile_type::fg tile_type::bg) in the interval
         * [0,size].
         * The <B>offset</B> is automatically added to the tile index.
         * sprite offset dictates where each sprite should render in its tile
         * @throw std::exception On any error.
         */
        void load_tilejson_from_file( const JsonObject &config );
        /**
         * Helper function called by load.
         * @param pump_events Handle window events and refresh the screen when necessary.
         *        Please ensure that the tileset is not accessed when this method is
         *        executing if you set it to true.
         * @throw std::exception On any error.
         */
        void load_internal( const JsonObject &config, const std::string &tileset_root,
                            const std::string &img_path, bool pump_events );
    public:
        tileset_loader( tileset &ts, const SDL_Renderer_Ptr &r ) : ts( ts ), renderer( r ) {
        }
        /**
         * @throw std::exception On any error.
         * @param tileset_id Ident of the tileset, as it appears in the options.
         * @param precheck If tue, only loads the meta data of the tileset (tile dimensions).
         * @param pump_events Handle window events and refresh the screen when necessary.
         *        Please ensure that the tileset is not accessed when this method is
         *        executing if you set it to true.
         */
        void load( const std::string &tileset_id, bool precheck, bool pump_events = false );
};

enum class text_alignment : int {
    left,
    center,
    right,
};

struct formatted_text {
    std::string text;
    int color;
    text_alignment alignment;

    formatted_text( const std::string &text, const int color, const text_alignment alignment )
        : text( text ), color( color ), alignment( alignment ) {
    }

    formatted_text( const std::string &text, int color, direction text_direction );
};

class idle_animation_manager
{
    private:
        int frame = 0;
        bool enabled_ = false;
        bool present_ = false;

    public:
        /** Set whether idle animations are enabled. */
        void set_enabled( bool enabled ) {
            enabled_ = enabled;
        }

        /** Prepare for redraw (clear cache, advance frame) */
        void prepare_for_redraw();

        /** Whether idle animations are enabled */
        bool enabled() const {
            return enabled_;
        }

        /** Current animation frame (increments by approx. 60 per second) */
        int current_frame() const {
            return frame;
        }

        /** Mark presence of an idle animation on screen */
        void mark_present() {
            present_ = true;
        }

        /** Whether there are idle animations on screen */
        bool present() const {
            return present_;
        }
};

/** type used for color blocks overlays.
 * first: The SDL blend mode used for the color.
 * second:
 *     - A point where to draw the color block (x, y)
 *     - The color of the block at 'point'.
 */
using color_block_overlay_container = std::pair<SDL_BlendMode, std::multimap<point, SDL_Color>>;

struct tile_render_info;

struct tile_search_result {
    const tile_type *tt;
    std::string found_id;
};

class cata_tiles
{
    public:
        cata_tiles( const SDL_Renderer_Ptr &render, const GeometryRenderer_Ptr &geometry );
        ~cata_tiles();

        /** Reload tileset, with the given scale. Scale is divided by 16 to allow for scales < 1 without risking
         *  float inaccuracies. */
        void set_draw_scale( float scale );

        /** Tries to find tile with specified parameters and return it if exists **/
        std::optional<tile_search_result> tile_type_search(
            const std::string &id, TILE_CATEGORY category, const std::string &subcategory,
            int subtile, int rota
        );

        void on_options_changed();

        /** Draw to screen */
        void draw( point dest, const tripoint &center, int width, int height,
                   std::multimap<point, formatted_text> &overlay_strings,
                   color_block_overlay_container &color_blocks );
        void draw_om( point dest, const tripoint_abs_omt &center_abs_omt, bool blink );

        bool terrain_requires_animation() const;

        /** Simply displays character on a screen with given X,Y position **/
        void display_character( const Character &ch, const point &p );

        /** Minimap functionality */
        void draw_minimap( point dest, const tripoint &center, int width, int height );
        bool minimap_requires_animation() const;

    protected:
        /** How many rows and columns of tiles fit into given dimensions **/
        void get_window_tile_counts( int width, int height, int &columns, int &rows ) const;

        std::optional<tile_lookup_res> find_tile_with_season( const std::string &id ) const;

        std::optional<tile_lookup_res>
        find_tile_looks_like( const std::string &id, TILE_CATEGORY category,
                              int looks_like_jumps_limit = 10 ) const;

        // this templated method is used only from it's own cpp file, so it's ok to declare it here
        template<typename T>
        std::optional<tile_lookup_res>
        find_tile_looks_like_by_string_id( const std::string &id, TILE_CATEGORY category,
                                           int looks_like_jumps_limit ) const;


        bool find_overlay_looks_like( bool male, const std::string &overlay, std::string &draw_id );

        /**
         * @brief draw_from_id_string() without category, subcategory and height_3d
         *
         * @param category C_NONE
         * @param subcategory empty_string
         * @param height_3d nullint
         */
        bool draw_from_id_string( const std::string &id, const tripoint &pos, int subtile, int rota,
                                  lit_level ll, bool apply_night_vision_goggles, int overlay_count );
        /**
         * @brief * @brief draw_from_id_string() without height_3d
         *
         * @param height_3d nullint
         */
        bool draw_from_id_string( const std::string &id, TILE_CATEGORY category,
                                  const std::string &subcategory, const tripoint &pos, int subtile, int rota,
                                  lit_level ll, bool apply_night_vision_goggles, int overlay_count );
        /**
         * @brief draw_from_id_string() without height_3d
         *
         * @param category C_NONE
         * @param subcategory empty_string
         */
        bool draw_from_id_string( const std::string &id, const tripoint &pos, int subtile, int rota,
                                  lit_level ll, bool apply_night_vision_goggles, int &height_3d, int overlay_count );
        /**
         * @brief Try to draw a tile using the given id. calls draw_tile_at() at the end.
         *
         * @param id String id of the tile to draw.
         * @param category Category of the tile to draw.
         * @param subcategory if id is not found, try to find a tile for the category+subcategory combination
         * @param pos Tripoint of the tile to draw.
         * @param subtile variant of the tile
         * @param rota rotation: { UP = 0, LEFT = 1, DOWN = 2, RIGHT = 3 }
         * @param ll light level
         * @param apply_night_vision_goggles use night vision colors?
         * @param height_3d return parameter for height of the sprite
         * @param overlay_count how blue the tile looks for lower z levels
         * @param as_independent_entity draw tile as single entity to the screen
         *                              (like if you would to display something unrelated to game map context
         *                              e.g. character preview tile in character creation screen)
         * @return always true
         */
        bool draw_from_id_string( const std::string &id, TILE_CATEGORY category,
                                  const std::string &subcategory, const tripoint &pos, int subtile, int rota,
                                  lit_level ll, bool apply_night_vision_goggles, int &height_3d, int overlay_count,
                                  bool as_independent_entity = false );
        /**
        * @brief Draw overmap tile, if it's transparent, then draw lower tile first
        *
        * @param id String id of the tile to draw.
        * @param rotation { UP = 0, LEFT = 1, DOWN = 2, RIGHT = 3 }
        * @param subtile variant of the tile
        * @param base_z_offset Z offset from given position, used to calculate overlay opacity
        */
        void draw_om_tile_recursively( const tripoint_abs_omt omp, const std::string &id, int rotation,
                                       int subtile, int base_z_offset );

        /**
         * @brief draw_sprite_at() without height_3d
         */
        bool draw_sprite_at(
            const tile_type &tile, const weighted_int_list<std::vector<int>> &svlist,
            point, unsigned int loc_rand, bool rota_fg, int rota, lit_level ll,
            bool apply_night_vision_goggles, int overlay_count );

        /**
         * @brief Try to draw either forground or background using the given reference.
         *
         * @param svlist list of weighted subtile variants
         * @param rota_fg is it foreground (true) or background?
         * @return always true.
         */
        bool draw_sprite_at(
            const tile_type &tile, const weighted_int_list<std::vector<int>> &svlist,
            point, unsigned int loc_rand, bool rota_fg, int rota, lit_level ll,
            bool apply_night_vision_goggles, int &height_3d, int overlay_alpha );

        /**
         * @brief Calls draw_sprite_at() twice each for foreground and background.
         *
         * @param tile Tile to draw.
         * @param p Point to draw the tile at.
         * @param loc_rand picked random int
         * @param rota_fg rotate foreground: { UP = 0, LEFT = 1, DOWN = 2, RIGHT = 3 }
         * @param rota rotation: { UP = 0, LEFT = 1, DOWN = 2, RIGHT = 3 }
         * @param ll light level
         * @param apply_night_vision_goggles use night vision colors?
         * @param height_3d return parameter for height of the sprite
         * @param overlay_count how blue the tile looks for lower z levels
         * @return always true.
         */
        bool draw_tile_at( const tile_type &tile, point, unsigned int loc_rand, int rota,
                           lit_level ll, bool apply_night_vision_goggles, int &height_3d, int overlay_count );

        /**
         * @brief Draws a colored solid color tile at position, with optional blending
         *
         * @param color Color to draw.
         * @param p Point to draw the tile at.
         * @param blend_mode Blend mode to draw the tile with
         * @return always true.
         */
        bool draw_color_at( const SDL_Color &color, point p,
                            SDL_BlendMode blend_mode = SDL_BLENDMODE_NONE );

        /** Tile Picking */
        void get_tile_values( int t, const int *tn, int &subtile, int &rotation );

        // as get_tile_values, but for unconnected tiles, infer rotation from surrouding walls
        void get_tile_values_with_ter( const tripoint &p, int t, const int *tn, int &subtile,
                                       int &rotation );

        void get_connect_values( const tripoint &p, int &subtile, int &rotation, int connect_group,
                                 const std::map<tripoint, ter_id> &ter_override );

        void get_furn_connect_values( const tripoint &p, int &subtile, int &rotation,
                                      int connect_group,
                                      const std::map<tripoint, furn_id> &furn_override );

        void get_terrain_orientation( const tripoint &p, int &rota, int &subtile,
                                      const std::map<tripoint, ter_id> &ter_override,
                                      const bool ( &invisible )[5] );

        void get_rotation_and_subtile( char val, int &rota, int &subtile );

        /** Map memory */
        bool has_memory_at( const tripoint &p ) const;
        bool has_terrain_memory_at( const tripoint &p ) const;
        bool has_furniture_memory_at( const tripoint &p ) const;
        bool has_trap_memory_at( const tripoint &p ) const;
        bool has_vpart_memory_at( const tripoint &p ) const;
        memorized_terrain_tile get_terrain_memory_at( const tripoint &p ) const;
        memorized_terrain_tile get_furniture_memory_at( const tripoint &p ) const;
        memorized_terrain_tile get_trap_memory_at( const tripoint &p ) const;
        memorized_terrain_tile get_vpart_memory_at( const tripoint &p ) const;

        /** Drawing Layers */
        bool would_apply_vision_effects( visibility_type visibility ) const;
        bool apply_vision_effects( const tripoint &pos, visibility_type visibility );

        bool draw_block( const tripoint &p, SDL_Color color, int scale );

        bool draw_terrain( const tripoint &p, lit_level ll, int &height_3d,
                           const bool ( &invisible )[5], int z_drop );
        bool draw_furniture( const tripoint &p, lit_level ll, int &height_3d,
                             const bool ( &invisible )[5], int z_drop );
        bool draw_graffiti( const tripoint &p, lit_level ll, int &height_3d,
                            const bool ( &invisible )[5], int z_drop );
        bool draw_trap( const tripoint &p, lit_level ll, int &height_3d,
                        const bool ( &invisible )[5], int z_drop );
        bool draw_field_or_item( const tripoint &p, lit_level ll, int &height_3d,
                                 const bool ( &invisible )[5], int z_drop );
        bool draw_vpart( const tripoint &p, lit_level ll, int &height_3d,
                         const bool ( &invisible )[5], int z_drop );
        bool draw_critter_at( const tripoint &p, lit_level ll, int &height_3d,
                              const bool ( &invisible )[5], int z_drop );
        bool draw_zone_mark( const tripoint &p, lit_level ll, int &height_3d,
                             const bool ( &invisible )[5], int z_drop );
        bool draw_zombie_revival_indicators( const tripoint &pos, lit_level ll, int &height_3d,
                                             const bool ( &invisible )[5], int z_drop );
        void draw_entity_with_overlays( const Character &ch, const tripoint &p, lit_level ll,
                                        int &height_3d, bool as_independent_entity = false );


        bool draw_item_highlight( const tripoint &pos );

    public:
        // Animation layers
        void init_explosion( const tripoint &p, int radius, const std::string &name );
        void draw_explosion_frame();
        void void_explosion();

        void init_custom_explosion_layer( const std::map<tripoint, explosion_tile> &layer,
                                          const std::string &name );
        void draw_custom_explosion_frame();
        void void_custom_explosion();

        void init_draw_cone_aoe( const tripoint &origin, const one_bucket &layer );
        void draw_cone_aoe_frame();
        void void_cone_aoe();

        void init_draw_bullet( const tripoint &p, std::string name, int rotation );
        void draw_bullet_frame();
        void void_bullet();

        void init_draw_hit( const tripoint &p, std::string name );
        void draw_hit_frame();
        void void_hit();

        void draw_footsteps_frame( const tripoint &center );

        // pseudo-animated layer, not really though.
        void init_draw_line( const tripoint &p, std::vector<tripoint> trajectory,
                             std::string line_end_name, bool target_line );
        void draw_line();
        void void_line();

        void init_draw_cursor( const tripoint &p );
        void draw_cursor();
        void void_cursor();

        void init_draw_highlight( const tripoint &p );
        void draw_highlight();
        void void_highlight();

        void init_draw_weather( weather_printable weather, std::string name );
        void draw_weather_frame();
        void void_weather();

        void init_draw_sct();
        void draw_sct_frame( std::multimap<point, formatted_text> &overlay_strings );
        void void_sct();

        void init_draw_zones( const tripoint &start, const tripoint &end, const tripoint &offset );
        void draw_zones_frame();
        void void_zones();

        void init_draw_radiation_override( const tripoint &p, int rad );
        void void_radiation_override();

        void init_draw_terrain_override( const tripoint &p, const ter_id &id );
        void void_terrain_override();

        void init_draw_furniture_override( const tripoint &p, const furn_id &id );
        void void_furniture_override();

        void init_draw_graffiti_override( const tripoint &p, bool has );
        void void_graffiti_override();

        void init_draw_trap_override( const tripoint &p, const trap_id &id );
        void void_trap_override();

        void init_draw_field_override( const tripoint &p, const field_type_id &id );
        void void_field_override();

        void init_draw_item_override( const tripoint &p, const itype_id &id, const mtype_id &mid,
                                      bool hilite );
        void void_item_override();

        void init_draw_vpart_override( const tripoint &p, const vpart_id &id, int part_mod,
                                       units::angle veh_dir, bool hilite, point mount );
        void void_vpart_override();

        void init_draw_below_override( const tripoint &p, bool draw );
        void void_draw_below_override();

        void init_draw_monster_override( const tripoint &p, const mtype_id &id, int count,
                                         bool more, Attitude att );
        void void_monster_override();

        bool has_draw_override( const tripoint &p ) const;
    public:
        /**
         * Initialize the current tileset (load tile images, load mapping), using the current
         * tileset as it is set in the options.
         * @param tileset_id Ident of the tileset, as it appears in the options.
         * @param mod_list List of active world mods, for correct caching behavior.
         * @param precheck If true, only loads the meta data of the tileset (tile dimensions).
         * @param force If true, forces loading the tileset even if it is already loaded.
         * @param pump_events Handle window events and refresh the screen when necessary.
         *        Please ensure that the tileset is not accessed when this method is
         *        executing if you set it to true.
         * @throw std::exception On any error.
         */
        void load_tileset(
            const std::string &tileset_id,
            const std::vector<mod_id> &mod_list,
            bool precheck = false,
            bool force = false,
            bool pump_events = false
        );
        /**
         * Reinitializes the current tileset, like @ref init, but using the original screen information.
         * @throw std::exception On any error.
         */
        void reinit();

        int get_tile_height() const {
            return tile_height;
        }
        int get_tile_width() const {
            return tile_width;
        }
        float get_tile_ratiox() const {
            return tile_ratiox;
        }
        float get_tile_ratioy() const {
            return tile_ratioy;
        }
        void do_tile_loading_report( const std::function<void( std::string )> &out );
        point player_to_screen( point ) const;
        static std::vector<options_manager::id_and_option> build_renderer_list();
        static std::vector<options_manager::id_and_option> build_display_list();
    private:
        std::string get_omt_id_rotation_and_subtile(
            const tripoint_abs_omt &omp, int &rota, int &subtile );
    protected:
        template <typename maptype>
        void tile_loading_report( const maptype &tiletypemap, TILE_CATEGORY category,
                                  std::function<void( std::string )> out, const std::string &prefix = "" );
        template <typename arraytype>
        void tile_loading_report( const arraytype &array, int array_length, TILE_CATEGORY category,
                                  std::function<void( std::string )> out, const std::string &prefix = "" );
        template <typename basetype>
        void tile_loading_report( size_t count, TILE_CATEGORY category,
                                  std::function<void( std::string )> out,
                                  const std::string &prefix );
        /**
         * Generic tile_loading_report, begin and end are iterators, id_func translates the iterator
         * to an id string (result of id_func must be convertible to string).
         */
        template<typename Iter, typename Func>
        void lr_generic( Iter begin, Iter end, Func id_func, TILE_CATEGORY category,
                         std::function<void( std::string )> out, const std::string &prefix );
        /** Lighting */
        void init_light();

        /** Variables */
        const SDL_Renderer_Ptr &renderer;
        const GeometryRenderer_Ptr &geometry;
        /** Currently loaded tileset. */
        std::unique_ptr<tileset> tileset_ptr;
        /** List of mods with which @ref tileset_ptr was loaded. */
        std::vector<mod_id> tileset_mod_list_stamp;

        int tile_height = 0;
        int tile_width = 0;
        // The width and height of the area we can draw in,
        // measured in map coordinates, *not* in pixels.
        int screentile_width = 0;
        int screentile_height = 0;
        float tile_ratiox = 0.0f;
        float tile_ratioy = 0.0f;

        idle_animation_manager idle_animations;

        bool in_animation = false;

        bool do_draw_explosion = false;
        bool do_draw_custom_explosion = false;
        bool do_draw_bullet = false;
        bool do_draw_hit = false;
        bool do_draw_line = false;
        bool do_draw_cursor = false;
        bool do_draw_highlight = false;
        bool do_draw_weather = false;
        bool do_draw_sct = false;
        bool do_draw_zones = false;
        bool do_draw_cone_aoe = false;

        tripoint exp_pos;
        int exp_rad = 0;
        std::string exp_name;

        std::map<tripoint, explosion_tile> custom_explosion_layer;

        tripoint cone_aoe_origin;
        one_bucket cone_aoe_layer;

        tripoint bul_pos;
        std::string bul_id;
        int bul_rotation = 0;

        tripoint hit_pos;
        std::string hit_entity_id;

        tripoint line_pos;
        bool is_target_line = false;
        std::vector<tripoint> line_trajectory;
        std::string line_endpoint_id;

        std::vector<tripoint> cursors;
        std::vector<tripoint> highlights;

        weather_printable anim_weather;
        std::string weather_name;

        tripoint zone_start;
        tripoint zone_end;
        tripoint zone_offset;

        // offset values, in tile coordinates, not pixels
        point o;
        // offset for drawing, in pixels.
        point op;

        std::map<tripoint, int> radiation_override;
        std::map<tripoint, ter_id> terrain_override;
        std::map<tripoint, furn_id> furniture_override;
        std::map<tripoint, bool> graffiti_override;
        std::map<tripoint, trap_id> trap_override;
        std::map<tripoint, field_type_id> field_override;
        // bool represents item highlight
        std::map<tripoint, std::tuple<itype_id, mtype_id, bool>> item_override;
        // int, angle, bool represents part_mod, veh_dir, and highlight respectively
        // point represents the mount direction
        std::map<tripoint, std::tuple<vpart_id, int, units::angle, bool, point>> vpart_override;
        std::map<tripoint, bool> draw_below_override;
        // int represents spawn count
        std::map<tripoint, std::tuple<mtype_id, int, bool, Attitude>> monster_override;
        pimpl<std::vector<tile_render_info>> draw_points_cache;

    private:
        /**
         * Tracks active night vision goggle status for each draw call.
         * Allows usage of night vision tilesets during sprite rendering.
         */
        bool nv_goggles_activated = false;

        pimpl<pixel_minimap> minimap;

    public:
        std::string memory_map_mode = "color_pixel_sepia";
};


