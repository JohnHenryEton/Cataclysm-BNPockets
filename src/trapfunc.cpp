#include "trap.h" // IWYU pragma: associated

#include <algorithm>
#include <cassert>
#include <memory>
#include <unordered_map>
#include <utility>

#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "character.h"
#include "creature.h"
#include "damage.h"
#include "debug.h"
#include "enums.h"
#include "explosion.h"
#include "character_functions.h"
#include "game.h"
#include "game_constants.h"
#include "int_id.h"
#include "item.h"
#include "map.h"
#include "map_iterator.h"
#include "mapdata.h"
#include "mapgen_functions.h"
#include "messages.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "output.h"
#include "player.h"
#include "point.h"
#include "rng.h"
#include "sounds.h"
#include "teleport.h"
#include "timed_event.h"
#include "translations.h"

static const skill_id skill_throw( "throw" );

static const species_id ROBOT( "ROBOT" );

static const bionic_id bio_shock_absorber( "bio_shock_absorber" );

static const efftype_id effect_beartrap( "beartrap" );
static const efftype_id effect_heavysnare( "heavysnare" );
static const efftype_id effect_in_pit( "in_pit" );
static const efftype_id effect_lightsnare( "lightsnare" );
static const efftype_id effect_ridden( "ridden" );
static const efftype_id effect_slimed( "slimed" );

static const itype_id itype_bullwhip( "bullwhip" );
static const itype_id itype_grapnel( "grapnel" );
static const itype_id itype_rope_30( "rope_30" );

static const trait_id trait_WINGS_BIRD( "WINGS_BIRD" );
static const trait_id trait_WINGS_BUTTERFLY( "WINGS_BUTTERFLY" );
static const trait_id trait_WEB_RAPPEL( "WEB_RAPPEL" );
static const trait_id trait_DEBUG_NOCLIP( "DEBUG_NOCLIP" );
static const trait_id trait_DEBUG_FLIGHT( "DEBUG_FLIGHT" );

static const mtype_id mon_blob( "mon_blob" );
static const mtype_id mon_shadow( "mon_shadow" );
static const mtype_id mon_shadow_snake( "mon_shadow_snake" );

// A pit becomes less effective as it fills with corpses.
static float pit_effectiveness( const tripoint &p )
{
    units::volume corpse_volume = 0_ml;
    for( auto &pit_content : g->m.i_at( p ) ) {
        if( pit_content->is_corpse() ) {
            corpse_volume += pit_content->volume();
        }
    }

    // 10 zombies; see item::volume
    const units::volume filled_volume = 10 * units::from_milliliter<float>( 62500 );

    return std::max( 0.0f, 1.0f - corpse_volume / filled_volume );
}

bool trapfunc::none( const tripoint &, Creature *, item * )
{
    return false;
}

bool trapfunc::bubble( const tripoint &p, Creature *c, item * )
{
    // tiny animals don't trigger bubble wrap
    if( c != nullptr && c->get_size() == creature_size::tiny ) {
        return false;
    }
    if( c != nullptr ) {
        c->add_msg_player_or_npc( m_warning, _( "You step on some bubble wrap!" ),
                                  _( "<npcname> steps on some bubble wrap!" ) );
        if( c->has_effect( effect_ridden ) ) {
            add_msg( m_warning, _( "Your %s steps on some bubble wrap!" ), c->get_name() );
        }
    }
    sounds::sound( p, 18, sounds::sound_t::alarm, _( "Pop!" ), false, "trap", "bubble_wrap" );
    g->m.tr_at( p ).trigger_aftermath( g->m, p );
    return true;
}

bool trapfunc::glass( const tripoint &p, Creature *c, item * )
{
    if( c != nullptr ) {
        // tiny animals and hallucinations don't trigger glass trap
        if( c->get_size() == creature_size::tiny || c->is_hallucination() ) {
            return false;
        }
        c->add_msg_player_or_npc( m_warning, _( "You step on some glass!" ),
                                  _( "<npcname> steps on some glass!" ) );

        monster *z = dynamic_cast<monster *>( c );
        const char dmg = std::max( 0, rng( -10, 10 ) );
        if( z != nullptr && dmg > 0 ) {
            z->moves -= 80;
        }
        if( dmg > 0 ) {
            c->deal_damage( nullptr, bodypart_id( "foot_l" ), damage_instance( DT_CUT, dmg ) );
            c->deal_damage( nullptr, bodypart_id( "foot_r" ), damage_instance( DT_CUT, dmg ) );
            c->check_dead_state();
        }
    }
    sounds::sound( p, 10, sounds::sound_t::combat, _( "glass cracking!" ), false, "trap", "glass" );
    g->m.tr_at( p ).trigger_aftermath( g->m, p );
    return true;
}

bool trapfunc::cot( const tripoint &p, Creature *c, item * )
{
    monster *z = dynamic_cast<monster *>( c );
    if( z != nullptr ) {
        // Haha, only monsters stumble over a cot, humans are smart.
        add_msg( m_good, _( "The %s stumbles over the cot!" ), z->name() );
        c->moves -= 100;
        // If you want to allow the cot to tip over or something when some buffoon trips over it, sure.
        g->m.tr_at( p ).trigger_aftermath( g->m, p );
        return true;
    }
    return false;
}

bool trapfunc::beartrap( const tripoint &p, Creature *c, item * )
{
    // tiny animals don't trigger bear traps
    if( c != nullptr && c->get_size() == creature_size::tiny ) {
        return false;
    }
    sounds::sound( p, 8, sounds::sound_t::combat, _( "SNAP!" ), false, "trap", "bear_trap" );
    if( c != nullptr ) {
        // What got hit?
        const bodypart_id hit = one_in( 2 ) ? bodypart_id( "leg_l" ) : bodypart_id( "leg_r" );

        // Messages
        c->add_msg_player_or_npc( m_bad, _( "A bear trap closes on your foot!" ),
                                  _( "A bear trap closes on <npcname>'s foot!" ) );
        if( c->has_effect( effect_ridden ) ) {
            add_msg( m_warning, _( "Your %s is caught by a beartrap!" ), c->get_name() );
        }
        // Actual effects
        c->add_effect( effect_beartrap, 1_turns, hit.id() );
        damage_instance d;
        d.add_damage( DT_BASH, 12 );
        d.add_damage( DT_CUT, 18 );
        c->deal_damage( nullptr, hit, d );
        c->check_dead_state();
    } else {
        g->m.spawn_item( p, "beartrap" );
    }
    // Limited utility given the item spawns when the victim breaks free, but allow it anyway.
    g->m.tr_at( p ).trigger_aftermath( g->m, p );
    return true;
}

bool trapfunc::board( const tripoint &p, Creature *c, item * )
{
    if( c == nullptr ) {
        return false;
    }
    // tiny animals don't trigger spiked boards, they can squeeze between the nails
    if( c->get_size() == creature_size::tiny ) {
        return false;
    }
    c->add_msg_player_or_npc( m_bad, _( "You step on a spiked board!" ),
                              _( "<npcname> steps on a spiked board!" ) );
    monster *z = dynamic_cast<monster *>( c );
    if( z != nullptr ) {
        if( z->has_effect( effect_ridden ) ) {
            add_msg( m_warning, _( "Your %s stepped on a spiked board!" ), c->get_name() );
            g->u.moves -= 80;
        } else {
            z->moves -= 80;
        }
        z->deal_damage( nullptr, bodypart_id( "foot_l" ), damage_instance( DT_CUT, rng( 3, 5 ) ) );
        z->deal_damage( nullptr, bodypart_id( "foot_r" ), damage_instance( DT_CUT, rng( 3, 5 ) ) );
    } else {
        c->deal_damage( nullptr, bodypart_id( "foot_l" ), damage_instance( DT_CUT, rng( 6, 10 ) ) );
        c->deal_damage( nullptr, bodypart_id( "foot_r" ), damage_instance( DT_CUT, rng( 6, 10 ) ) );
    }
    c->check_dead_state();
    g->m.tr_at( p ).trigger_aftermath( g->m, p );
    return true;
}

bool trapfunc::caltrops( const tripoint &p, Creature *c, item * )
{
    if( c == nullptr ) {
        return false;
    }
    // tiny animals don't trigger caltrops, they can squeeze between them
    if( c->get_size() == creature_size::tiny ) {
        return false;
    }
    c->add_msg_player_or_npc( m_bad, _( "You step on a sharp metal caltrop!" ),
                              _( "<npcname> steps on a sharp metal caltrop!" ) );
    monster *z = dynamic_cast<monster *>( c );
    if( z != nullptr ) {
        if( z->has_effect( effect_ridden ) ) {
            add_msg( m_warning, _( "Your %s steps on a sharp metal caltrop!" ), c->get_name() );
            g->u.moves -= 80;
        } else {
            z->moves -= 80;
        }
        c->deal_damage( nullptr, bodypart_id( "foot_l" ), damage_instance( DT_CUT, rng( 9, 15 ) ) );
        c->deal_damage( nullptr, bodypart_id( "foot_r" ), damage_instance( DT_CUT, rng( 9, 15 ) ) );
    } else {
        c->deal_damage( nullptr, bodypart_id( "foot_l" ), damage_instance( DT_CUT, rng( 9, 30 ) ) );
        c->deal_damage( nullptr, bodypart_id( "foot_r" ), damage_instance( DT_CUT, rng( 9, 30 ) ) );
    }
    c->check_dead_state();
    g->m.tr_at( p ).trigger_aftermath( g->m, p );
    return true;
}

bool trapfunc::caltrops_glass( const tripoint &p, Creature *c, item * )
{
    if( c == nullptr ) {
        return false;
    }
    // tiny animals don't trigger caltrops, they can squeeze between them
    if( c->get_size() == creature_size::tiny || c->is_hallucination() ) {
        return false;
    }
    c->add_msg_player_or_npc( m_bad, _( "You step on a sharp glass caltrop!" ),
                              _( "<npcname> steps on a sharp glass caltrop!" ) );
    monster *z = dynamic_cast<monster *>( c );
    if( z != nullptr ) {
        z->moves -= 80;
        c->deal_damage( nullptr, bodypart_id( "foot_l" ), damage_instance( DT_CUT, rng( 9, 15 ) ) );
        c->deal_damage( nullptr, bodypart_id( "foot_r" ), damage_instance( DT_CUT, rng( 9, 15 ) ) );
    } else {
        c->deal_damage( nullptr, bodypart_id( "foot_l" ), damage_instance( DT_CUT, rng( 9, 30 ) ) );
        c->deal_damage( nullptr, bodypart_id( "foot_r" ), damage_instance( DT_CUT, rng( 9, 30 ) ) );
    }
    c->check_dead_state();
    if( g->u.sees( p ) ) {
        add_msg( _( "The shards shatter!" ) );
        sounds::sound( p, 8, sounds::sound_t::combat, _( "glass cracking!" ), false, "trap",
                       "glass_caltrops" );
    }
    g->m.tr_at( p ).trigger_aftermath( g->m, p );
    return true;
}

bool trapfunc::tripwire( const tripoint &p, Creature *c, item * )
{
    if( c == nullptr ) {
        return false;
    }
    // tiny animals don't trigger tripwires, they just squeeze under it
    if( c->get_size() == creature_size::tiny ) {
        return false;
    }
    c->add_msg_player_or_npc( m_bad, _( "You trip over a tripwire!" ),
                              _( "<npcname> trips over a tripwire!" ) );
    monster *z = dynamic_cast<monster *>( c );
    player *n = dynamic_cast<player *>( c );
    if( z != nullptr ) {
        if( z->has_effect( effect_ridden ) ) {
            add_msg( m_bad, _( "Your %s trips over a tripwire!" ), z->get_name() );
            std::vector<tripoint> valid;
            for( const tripoint &jk : g->m.points_in_radius( p, 1 ) ) {
                if( g->is_empty( jk ) ) {
                    valid.push_back( jk );
                }
            }
            if( !valid.empty() ) {
                g->u.setpos( random_entry( valid ) );
                z->setpos( g->u.pos() );
            }
            g->u.moves -= 150;
            g->update_map( g->u );
        } else {
            z->stumble();
        }
        if( rng( 0, 10 ) > z->get_dodge() ) {
            z->deal_damage( nullptr, bodypart_id( "torso" ), damage_instance( DT_TRUE, rng( 1, 4 ) ) );
        }
    } else if( n != nullptr ) {
        std::vector<tripoint> valid;
        for( const tripoint &jk : g->m.points_in_radius( p, 1 ) ) {
            if( g->is_empty( jk ) ) {
                valid.push_back( jk );
            }
        }
        if( !valid.empty() ) {
            n->setpos( random_entry( valid ) );
        }
        n->moves -= 150;
        if( c == &g->u ) {
            g->update_map( g->u );
        }
        if( !n->is_mounted() ) {
            ///\EFFECT_DEX decreases chance of taking damage from a tripwire trap
            if( rng( 5, 20 ) > n->dex_cur ) {
                n->hurtall( rng( 1, 4 ), nullptr );
            }
        }
    }
    c->check_dead_state();
    g->m.tr_at( p ).trigger_aftermath( g->m, p );
    return true;
}

bool trapfunc::crossbow( const tripoint &p, Creature *c, item * )
{
    bool add_bolt = true;
    if( c != nullptr ) {
        if( c->has_effect( effect_ridden ) ) {
            add_msg( m_neutral, _( "Your %s triggers a crossbow trap." ), c->get_name() );
        }
        c->add_msg_player_or_npc( m_neutral, _( "You trigger a crossbow trap!" ),
                                  _( "<npcname> triggers a crossbow trap!" ) );
        monster *z = dynamic_cast<monster *>( c );
        player *n = dynamic_cast<player *>( c );
        if( n != nullptr ) {
            ///\EFFECT_DODGE reduces chance of being hit by crossbow trap
            if( !one_in( 4 ) && rng( 8, 20 ) > n->get_dodge() ) {
                bodypart_str_id hit;
                switch( rng( 1, 10 ) ) {
                    case  1:
                        if( one_in( 2 ) ) {
                            hit = body_part_foot_l;
                        } else {
                            hit = body_part_foot_r;
                        }
                        break;
                    case  2:
                    case  3:
                    case  4:
                        if( one_in( 2 ) ) {
                            hit = body_part_leg_l;
                        } else {
                            hit = body_part_leg_r;
                        }
                        break;
                    case  5:
                    case  6:
                    case  7:
                    case  8:
                    case  9:
                        hit = body_part_torso;
                        break;
                    case 10:
                        hit = body_part_head;
                        break;
                }
                //~ %s is bodypart
                n->add_msg_if_player( m_bad, _( "Your %s is hit!" ), body_part_name( hit->token ) );
                c->deal_damage( nullptr, hit, damage_instance( DT_STAB, rng( 30, 45 ) ) );
                add_bolt = !one_in( 10 );
            } else {
                n->add_msg_player_or_npc( m_neutral, _( "You dodge the shot!" ),
                                          _( "<npcname> dodges the shot!" ) );
            }
        } else if( z != nullptr ) {
            bool seen = g->u.sees( *z );
            int chance = 0;
            // chance of getting hit depends on size
            switch( z->type->size ) {
                case creature_size::tiny:
                    chance = 50;
                    break;
                case creature_size::small:
                    chance = 8;
                    break;
                case creature_size::medium:
                    chance = 6;
                    break;
                case creature_size::large:
                    chance = 4;
                    break;
                case creature_size::huge:
                    chance = 1;
                    break;
                default:
                    break;
            }
            if( one_in( chance ) ) {
                if( seen ) {
                    add_msg( m_bad, _( "A bolt shoots out and hits the %s!" ), z->name() );
                }
                z->deal_damage( nullptr, body_part_torso.id(), damage_instance( DT_STAB, rng( 30, 45 ) ) );
                add_bolt = !one_in( 10 );
            } else if( seen ) {
                add_msg( m_neutral, _( "A bolt shoots out, but misses the %s." ), z->name() );
            }
        }
        c->check_dead_state();
    }
    // No support for randomization of trigger_items yet, so special-case the ammo drop.
    if( add_bolt ) {
        g->m.spawn_item( p, "bolt_steel", 1, 1 );
    }
    g->m.tr_at( p ).trigger_aftermath( g->m, p );
    return true;
}

bool trapfunc::shotgun( const tripoint &p, Creature *c, item * )
{
    sounds::sound( p, 60, sounds::sound_t::combat, _( "Kerblam!" ), false, "fire_gun",
                   g->m.tr_at( p ).loadid == tr_shotgun_1 ? "shotgun_s" : "shotgun_d" );
    int shots = ( g->m.tr_at( p ).loadid == tr_shotgun_2 ? 2 : 1 );
    if( c != nullptr ) {
        if( c->has_effect( effect_ridden ) ) {
            add_msg( m_neutral, _( "Your %s triggers a shotgun trap!" ), c->get_name() );
        }
        c->add_msg_player_or_npc( m_neutral, _( "You trigger a shotgun trap!" ),
                                  _( "<npcname> triggers a shotgun trap!" ) );
        monster *z = dynamic_cast<monster *>( c );
        player *n = dynamic_cast<player *>( c );
        if( n != nullptr ) {
            ///\EFFECT_DODGE reduces chance of being hit by shotgun trap
            if( rng( 5, 50 ) > n->get_dodge() ) {
                bodypart_str_id hit;
                switch( rng( 1, 10 ) ) {
                    case  1:
                        if( one_in( 2 ) ) {
                            hit = body_part_foot_l;
                        } else {
                            hit = body_part_foot_r;
                        }
                        break;
                    case  2:
                    case  3:
                    case  4:
                        if( one_in( 2 ) ) {
                            hit = body_part_leg_l;
                        } else {
                            hit = body_part_leg_r;
                        }
                        break;
                    case  5:
                    case  6:
                    case  7:
                    case  8:
                    case  9:
                        hit = body_part_torso;
                        break;
                    case 10:
                        hit = body_part_head;
                        break;
                }
                //~ %s is bodypart
                n->add_msg_if_player( m_bad, _( "Your %s is hit!" ), body_part_name( hit->token ) );
                // Unload both barrels (if present) as two separate attacks, generic damage stats as could be using slugs or shot
                if( shots > 1 ) {
                    c->deal_damage( nullptr, hit, damage_instance( DT_BULLET, rng( 60, 80 ), 0, 1.5f ) );
                }
                c->deal_damage( nullptr, hit, damage_instance( DT_BULLET, rng( 60, 80 ), 0, 1.5f ) );
            } else {
                n->add_msg_player_or_npc( m_neutral, _( "You dodge the shot!" ),
                                          _( "<npcname> dodges the shot!" ) );
            }
        } else if( z != nullptr ) {
            bool seen = g->u.sees( *z );
            if( seen ) {
                add_msg( m_bad, _( "A shotgun fires and hits the %s!" ), z->name() );
            }
            if( shots > 1 ) {
                z->deal_damage( nullptr, body_part_torso.id(), damage_instance( DT_BULLET, rng( 60,
                                80 ), 0, 1.5f ) );
            }
            z->deal_damage( nullptr, body_part_torso.id(), damage_instance( DT_BULLET, rng( 60,
                            80 ), 0, 1.5f ) );
        }
        c->check_dead_state();
    }

    g->m.tr_at( p ).trigger_aftermath( g->m, p );
    return true;
}

bool trapfunc::blade( const tripoint &p, Creature *c, item * )
{
    if( c == nullptr ) {
        return false;
    }
    if( c->has_effect( effect_ridden ) ) {
        add_msg( m_bad, _( "A blade swings out and hacks your %s!" ), c->get_name() );
    }
    c->add_msg_player_or_npc( m_bad, _( "A blade swings out and hacks your torso!" ),
                              _( "A blade swings out and hacks <npcname>s torso!" ) );
    damage_instance d;
    d.add_damage( DT_BASH, 12 );
    d.add_damage( DT_CUT, 30 );
    c->deal_damage( nullptr, bodypart_id( "torso" ), d );
    c->check_dead_state();
    g->m.tr_at( p ).trigger_aftermath( g->m, p );
    return true;
}

bool trapfunc::snare_light( const tripoint &p, Creature *c, item * )
{
    sounds::sound( p, 2, sounds::sound_t::combat, _( "Snap!" ), false, "trap", "snare" );
    if( c == nullptr ) {
        return false;
    }
    // Determine what gets hit
    const bodypart_id hit = one_in( 2 ) ? bodypart_id( "leg_l" ) : bodypart_id( "leg_r" );
    // Messages
    if( c->has_effect( effect_ridden ) ) {
        add_msg( m_bad, _( "A snare closes on your %s's leg!" ), c->get_name() );
    }
    c->add_msg_player_or_npc( m_bad, _( "A snare closes on your leg." ),
                              _( "A snare closes on <npcname>s leg." ) );

    // Actual effects
    c->add_effect( effect_lightsnare, 1_turns, hit.id() );
    monster *z = dynamic_cast<monster *>( c );
    if( z != nullptr && z->type->size == creature_size::tiny ) {
        z->deal_damage( nullptr, hit, damage_instance( DT_BASH, 10 ) );
    }
    c->check_dead_state();
    // Limited utility given the item spawns when the victim breaks free, but allow it anyway.
    g->m.tr_at( p ).trigger_aftermath( g->m, p );
    return true;
}

bool trapfunc::snare_heavy( const tripoint &p, Creature *c, item * )
{
    sounds::sound( p, 4, sounds::sound_t::combat, _( "Snap!" ), false, "trap", "snare" );
    if( c == nullptr ) {
        return false;
    }
    // Determine what got hit
    const bodypart_id hit = one_in( 2 ) ? bodypart_id( "leg_l" ) : bodypart_id( "leg_r" );
    if( c->has_effect( effect_ridden ) ) {
        add_msg( m_bad, _( "A snare closes on your %s's leg!" ), c->get_name() );
    }
    //~ %s is bodypart name in accusative.
    c->add_msg_player_or_npc( m_bad, _( "A snare closes on your %s." ),
                              _( "A snare closes on <npcname>s %s." ), body_part_name_accusative( hit->token ) );

    // Actual effects
    c->add_effect( effect_heavysnare, 1_turns, hit.id() );
    monster *z = dynamic_cast<monster *>( c );
    player *n = dynamic_cast<player *>( c );
    if( n != nullptr ) {
        damage_instance d;
        d.add_damage( DT_BASH, 10 );
        n->deal_damage( nullptr, hit, d );
    } else if( z != nullptr ) {
        int damage;
        switch( z->type->size ) {
            case creature_size::tiny:
            case creature_size::small:
                damage = 20;
                break;
            case creature_size::medium:
                damage = 10;
                break;
            default:
                damage = 0;
        }
        z->deal_damage( nullptr, hit, damage_instance( DT_BASH, damage ) );
    }
    c->check_dead_state();
    // Limited utility given the item spawns when the victim breaks free, but allow it anyway.
    g->m.tr_at( p ).trigger_aftermath( g->m, p );
    return true;
}

static explosion_data get_basic_explosion_data()
{
    // equivalent of grenade explosion
    explosion_data data;
    data.damage = 40;
    data.radius = 3;
    projectile fragment;
    fragment.range = 6;
    fragment.speed = 1000;
    fragment.impact.add_damage( DT_BULLET, 80, 0, 3.0F );
    data.fragment = fragment;
    return data;
}

bool trapfunc::landmine( const tripoint &p, Creature *c, item * )
{
    // tiny animals are too light to trigger land mines
    if( c != nullptr && c->get_size() == creature_size::tiny ) {
        return false;
    }
    if( c != nullptr ) {
        c->add_msg_player_or_npc( m_bad, _( "You trigger a land mine!" ),
                                  _( "<npcname> triggers a land mine!" ) );
    }
    explosion_handler::explosion( p, get_basic_explosion_data(), nullptr );
    g->m.tr_at( p ).trigger_aftermath( g->m, p );
    return true;
}

bool trapfunc::boobytrap( const tripoint &p, Creature *c, item * )
{
    if( c != nullptr ) {
        c->add_msg_player_or_npc( m_bad, _( "You trigger a booby trap!" ),
                                  _( "<npcname> triggers a booby trap!" ) );
    }
    explosion_handler::explosion( p, get_basic_explosion_data(), nullptr );
    g->m.tr_at( p ).trigger_aftermath( g->m, p );
    return true;
}

bool trapfunc::telepad( const tripoint &p, Creature *c, item * )
{
    //~ the sound of a telepad functioning
    sounds::sound( p, 6, sounds::sound_t::movement, _( "vvrrrRRMM*POP!*" ), false, "trap", "teleport" );
    if( c == nullptr ) {
        return false;
    }
    if( c == &g->u ) {
        c->add_msg_if_player( m_warning, _( "The air shimmers around you…" ) );
    } else {
        if( g->u.sees( p ) ) {
            add_msg( _( "The air shimmers around %s…" ), c->disp_name() );
        }
    }
    // Do trap cleanup early because p is about to get changed by the victim teleporting.
    g->m.tr_at( p ).trigger_aftermath( g->m, p );
    teleport::teleport( *c );
    return true;
}

bool trapfunc::goo( const tripoint &p, Creature *c, item * )
{
    if( c == nullptr ) {
        return false;
    }
    c->add_msg_player_or_npc( m_bad, _( "You step in a puddle of thick goo." ),
                              _( "<npcname> steps in a puddle of thick goo." ) );
    monster *z = dynamic_cast<monster *>( c );
    player *n = dynamic_cast<player *>( c );
    if( n != nullptr ) {
        n->add_env_effect( effect_slimed, body_part_foot_l, 6, 2_minutes );
        n->add_env_effect( effect_slimed, body_part_foot_r, 6, 2_minutes );
        if( one_in( 3 ) ) {
            n->add_msg_if_player( m_bad, _( "The acidic goo eats away at your feet." ) );
            // @todo: Acid damage?
            n->deal_damage( nullptr, bodypart_id( "foot_l" ), damage_instance( DT_CUT, 5 ) );
            n->deal_damage( nullptr, bodypart_id( "foot_r" ), damage_instance( DT_CUT, 5 ) );
            n->check_dead_state();
        }
        g->m.tr_at( p ).trigger_aftermath( g->m, p );
        return true;
    } else if( z != nullptr ) {
        if( z->has_effect( effect_ridden ) ) {
            g->u.forced_dismount();
        }
        //All monsters except for blobs get a speed decrease
        if( z->type->id != mon_blob ) {
            z->set_speed_base( z->get_speed_base() - 15 );
            //All monsters that aren't blobs or robots transform into a blob
            if( !z->type->in_species( ROBOT ) ) {
                z->poly( mon_blob );
                z->set_hp( z->get_speed() );
            }
        } else {
            z->set_speed_base( z->get_speed_base() + 15 );
            z->set_hp( z->get_speed() );
        }
        g->m.tr_at( p ).trigger_aftermath( g->m, p );
        return true;
    }
    assert( false );
    return false;
}

bool trapfunc::dissector( const tripoint &p, Creature *c, item * )
{
    if( c == nullptr ) {
        return false;
    }
    monster *z = dynamic_cast<monster *>( c );
    if( z != nullptr ) {
        if( z->type->in_species( ROBOT ) ) {
            //The monster is a robot. So the dissector should not try to dissect the monsters flesh.
            //Dissector error sound.
            sounds::sound( p, 4, sounds::sound_t::electronic_speech,
                           _( "BEEPBOOP!  Please remove non-organic object." ), false, "speech", "robot" );
            c->add_msg_player_or_npc( m_bad, _( "The dissector lights up, and shuts down." ),
                                      _( "The dissector lights up, and shuts down." ) );
            return false;
        }
        // distribute damage amongst player and horse
        if( z->has_effect( effect_ridden ) && z->mounted_player ) {
            Character *ch = z->mounted_player;
            ch->deal_damage( nullptr, bodypart_id( "head" ), damage_instance( DT_CUT, 15 ) );
            ch->deal_damage( nullptr, bodypart_id( "torso" ), damage_instance( DT_CUT, 20 ) );
            ch->deal_damage( nullptr, bodypart_id( "arm_r" ), damage_instance( DT_CUT, 12 ) );
            ch->deal_damage( nullptr, bodypart_id( "arm_l" ), damage_instance( DT_CUT, 12 ) );
            ch->deal_damage( nullptr, bodypart_id( "hand_r" ), damage_instance( DT_CUT, 10 ) );
            ch->deal_damage( nullptr, bodypart_id( "hand_l" ), damage_instance( DT_CUT, 10 ) );
            ch->deal_damage( nullptr, bodypart_id( "leg_r" ), damage_instance( DT_CUT, 12 ) );
            ch->deal_damage( nullptr, bodypart_id( "leg_r" ), damage_instance( DT_CUT, 12 ) );
            ch->deal_damage( nullptr, bodypart_id( "foot_l" ), damage_instance( DT_CUT, 10 ) );
            ch->deal_damage( nullptr, bodypart_id( "foot_r" ), damage_instance( DT_CUT, 10 ) );
            if( g->u.sees( p ) ) {
                ch->add_msg_player_or_npc( m_bad, _( "Electrical beams emit from the floor and slice your flesh!" ),
                                           _( "Electrical beams emit from the floor and slice <npcname>s flesh!" ) );
            }
            ch->check_dead_state();
        }
    }

    //~ the sound of a dissector dissecting
    sounds::sound( p, 10, sounds::sound_t::combat, _( "BRZZZAP!" ), false, "trap", "dissector" );
    if( g->u.sees( p ) ) {
        add_msg( m_bad, _( "Electrical beams emit from the floor and slice the %s!" ), c->get_name() );
    }
    c->deal_damage( nullptr, bodypart_id( "head" ), damage_instance( DT_CUT, 15 ) );
    c->deal_damage( nullptr, bodypart_id( "torso" ), damage_instance( DT_CUT, 20 ) );
    c->deal_damage( nullptr, bodypart_id( "arm_r" ), damage_instance( DT_CUT, 12 ) );
    c->deal_damage( nullptr, bodypart_id( "arm_l" ), damage_instance( DT_CUT, 12 ) );
    c->deal_damage( nullptr, bodypart_id( "hand_r" ), damage_instance( DT_CUT, 10 ) );
    c->deal_damage( nullptr, bodypart_id( "hand_l" ), damage_instance( DT_CUT, 10 ) );
    c->deal_damage( nullptr, bodypart_id( "leg_r" ), damage_instance( DT_CUT, 12 ) );
    c->deal_damage( nullptr, bodypart_id( "leg_r" ), damage_instance( DT_CUT, 12 ) );
    c->deal_damage( nullptr, bodypart_id( "foot_l" ), damage_instance( DT_CUT, 10 ) );
    c->deal_damage( nullptr, bodypart_id( "foot_r" ), damage_instance( DT_CUT, 10 ) );

    c->check_dead_state();
    g->m.tr_at( p ).trigger_aftermath( g->m, p );
    return true;
}

bool trapfunc::pit( const tripoint &p, Creature *c, item * )
{
    if( c == nullptr ) {
        return false;
    }
    // tiny animals aren't hurt by falling into pits
    if( c->get_size() == creature_size::tiny ) {
        return false;
    }
    const float eff = pit_effectiveness( p );
    c->add_msg_player_or_npc( m_bad, _( "You fall in a pit!" ), _( "<npcname> falls in a pit!" ) );
    c->add_effect( effect_in_pit, 1_turns, bodypart_str_id::NULL_ID() );
    monster *z = dynamic_cast<monster *>( c );
    player *n = dynamic_cast<player *>( c );
    if( n != nullptr ) {
        if( ( n->has_trait( trait_WINGS_BIRD ) ) || ( ( one_in( 2 ) ) &&
                ( n->has_trait( trait_WINGS_BUTTERFLY ) ) ) ) {
            n->add_msg_if_player( _( "You flap your wings and flutter down gracefully." ) );
        } else if( n->has_trait( trait_WEB_RAPPEL ) ) {
            n->add_msg_if_player( _( "You quickly spin a line of silk and rappel down." ) );
        } else if( n->has_active_bionic( bio_shock_absorber ) ) {
            n->add_msg_if_player( m_info,
                                  _( "You hit the ground hard, but your shock absorbers handle the impact admirably!" ) );
        } else {
            int dodge = n->get_dodge();
            ///\EFFECT_DODGE reduces damage taken falling into a pit
            int damage = eff * rng( 10, 20 ) - rng( dodge, dodge * 5 );
            if( damage > 0 ) {
                n->add_msg_if_player( m_bad, _( "You hurt yourself!" ) );
                // like the message says \-:
                n->hurtall( rng( ( damage / 2 ), damage ), n );
                n->deal_damage( nullptr, bodypart_id( "leg_l" ), damage_instance( DT_BASH, damage ) );
                n->deal_damage( nullptr, bodypart_id( "leg_r" ), damage_instance( DT_BASH, damage ) );
            } else {
                n->add_msg_if_player( _( "You land nimbly." ) );
            }
        }
    } else if( z != nullptr ) {
        if( z->has_effect( effect_ridden ) ) {
            add_msg( m_bad, _( "Your %s falls into a pit!" ), z->get_name() );
            g->u.forced_dismount();
        }
        z->deal_damage( nullptr, bodypart_id( "leg_l" ), damage_instance( DT_BASH, eff * rng( 10, 20 ) ) );
        z->deal_damage( nullptr, bodypart_id( "leg_r" ), damage_instance( DT_BASH, eff * rng( 10, 20 ) ) );
    }
    c->check_dead_state();
    // If you really want to be able to remove this or spawn items on trigger, note this won't override pit terrain!
    g->m.tr_at( p ).trigger_aftermath( g->m, p );
    return true;
}

bool trapfunc::pit_spikes( const tripoint &p, Creature *c, item * )
{
    if( c == nullptr ) {
        return false;
    }
    // tiny animals aren't hurt by falling into spiked pits
    if( c->get_size() == creature_size::tiny ) {
        return false;
    }
    c->add_msg_player_or_npc( m_bad, _( "You fall in a spiked pit!" ),
                              _( "<npcname> falls in a spiked pit!" ) );
    c->add_effect( effect_in_pit, 1_turns, bodypart_str_id::NULL_ID() );
    monster *z = dynamic_cast<monster *>( c );
    player *n = dynamic_cast<player *>( c );
    if( n != nullptr ) {
        int dodge = n->get_dodge();
        int damage = pit_effectiveness( p ) * rng( 20, 50 );
        if( ( n->has_trait( trait_WINGS_BIRD ) ) || ( ( one_in( 2 ) ) &&
                ( n->has_trait( trait_WINGS_BUTTERFLY ) ) ) ) {
            n->add_msg_if_player( _( "You flap your wings and flutter down gracefully." ) );
        } else if( n->has_trait( trait_WEB_RAPPEL ) ) {
            n->add_msg_if_player( _( "You quickly spin a line of silk and rappel down." ) );
        } else if( n->has_active_bionic( bio_shock_absorber ) ) {
            n->add_msg_if_player( m_info,
                                  _( "You hit the ground hard, but your shock absorbers handle the impact admirably!" ) );
            ///\EFFECT_DODGE reduces chance of landing on spikes in spiked pit
        } else if( 0 == damage || rng( 5, 30 ) < dodge ) {
            n->add_msg_if_player( _( "You avoid the spikes within." ) );
        } else {
            bodypart_str_id hit;
            switch( rng( 1, 10 ) ) {
                case  1:
                    hit = body_part_leg_l;
                    break;
                case  2:
                    hit = body_part_leg_r;
                    break;
                case  3:
                    hit = body_part_arm_l;
                    break;
                case  4:
                    hit = body_part_arm_r;
                    break;
                case  5:
                case  6:
                case  7:
                case  8:
                case  9:
                case 10:
                    hit = body_part_torso;
                    break;
            }
            n->add_msg_if_player( m_bad, _( "The spikes impale your %s!" ),
                                  body_part_name_accusative( hit->token ) );
            n->deal_damage( nullptr, hit, damage_instance( DT_CUT, damage ) );
        }
    } else if( z != nullptr ) {
        if( z->has_effect( effect_ridden ) ) {
            add_msg( m_bad, _( "Your %s falls into a pit!" ), z->get_name() );
            g->u.forced_dismount();
        }
        z->deal_damage( nullptr, body_part_torso.id(), damage_instance( DT_CUT, rng( 20, 50 ) ) );
    }
    c->check_dead_state();
    if( one_in( 4 ) ) {
        if( g->u.sees( p ) ) {
            add_msg( _( "The spears break!" ) );
        }
        g->m.ter_set( p, t_pit );
        // 4 spears to a pit
        for( int i = 0; i < 4; i++ ) {
            if( one_in( 3 ) ) {
                g->m.spawn_item( p, "pointy_stick" );
            }
        }
    }
    // If you really want to be able to remove this or spawn items on trigger, note this won't override pit terrain!
    g->m.tr_at( p ).trigger_aftermath( g->m, p );
    return true;
}

bool trapfunc::pit_glass( const tripoint &p, Creature *c, item * )
{
    if( c == nullptr ) {
        return false;
    }
    // tiny animals aren't hurt by falling into glass pits
    if( c->get_size() == creature_size::tiny ) {
        return false;
    }
    c->add_msg_player_or_npc( m_bad, _( "You fall in a pit filled with glass shards!" ),
                              _( "<npcname> falls in pit filled with glass shards!" ) );
    c->add_effect( effect_in_pit, 1_turns, bodypart_str_id::NULL_ID() );
    monster *z = dynamic_cast<monster *>( c );
    player *n = dynamic_cast<player *>( c );
    if( n != nullptr ) {
        int dodge = n->get_dodge();
        int damage = pit_effectiveness( p ) * rng( 15, 35 );
        if( ( n->has_trait( trait_WINGS_BIRD ) ) || ( ( one_in( 2 ) ) &&
                ( n->has_trait( trait_WINGS_BUTTERFLY ) ) ) ) {
            n->add_msg_if_player( _( "You flap your wings and flutter down gracefully." ) );
        } else if( n->has_trait( trait_WEB_RAPPEL ) ) {
            n->add_msg_if_player( _( "You quickly spin a line of silk and rappel down." ) );
        } else if( n->has_active_bionic( bio_shock_absorber ) ) {
            n->add_msg_if_player( m_info,
                                  _( "You hit the ground hard, but your shock absorbers handle the impact admirably!" ) );
            ///\EFFECT_DODGE reduces chance of landing on glass in glass pit
        } else if( 0 == damage || rng( 5, 30 ) < dodge ) {
            n->add_msg_if_player( _( "You avoid the glass shards within." ) );
        } else {
            bodypart_str_id hit;
            switch( rng( 1, 10 ) ) {
                case  1:
                    hit = body_part_leg_l;
                    break;
                case  2:
                    hit = body_part_leg_r;
                    break;
                case  3:
                    hit = body_part_arm_l;
                    break;
                case  4:
                    hit = body_part_arm_r;
                    break;
                case  5:
                    hit = body_part_foot_l;
                    break;
                case  6:
                    hit = body_part_foot_r;
                    break;
                case  7:
                case  8:
                case  9:
                case 10:
                    hit = body_part_torso;
                    break;
            }
            n->add_msg_if_player( m_bad, _( "The glass shards slash your %s!" ),
                                  body_part_name_accusative( hit->token ) );
            n->deal_damage( nullptr, hit, damage_instance( DT_CUT, damage ) );
        }
    } else if( z != nullptr ) {
        if( z->has_effect( effect_ridden ) ) {
            add_msg( m_bad, _( "Your %s falls into a pit!" ), z->get_name() );
            g->u.forced_dismount();
        }
        z->deal_damage( nullptr, body_part_torso.id(), damage_instance( DT_CUT, rng( 20, 50 ) ) );
    }
    c->check_dead_state();
    if( one_in( 5 ) ) {
        if( g->u.sees( p ) ) {
            add_msg( _( "The shards shatter!" ) );
        }
        g->m.ter_set( p, t_pit );
        // 20 shards in a pit.
        for( int i = 0; i < 20; i++ ) {
            if( one_in( 3 ) ) {
                g->m.spawn_item( p, "glass_shard" );
            }
        }
    }
    // If you really want to be able to remove this or spawn items on trigger, note this won't override pit terrain!
    g->m.tr_at( p ).trigger_aftermath( g->m, p );
    return true;
}

bool trapfunc::lava( const tripoint &p, Creature *c, item * )
{
    if( c == nullptr ) {
        return false;
    }
    c->add_msg_player_or_npc( m_bad, _( "The %s burns you horribly!" ), _( "The %s burns <npcname>!" ),
                              g->m.tr_at( p ).name() );
    monster *z = dynamic_cast<monster *>( c );
    player *n = dynamic_cast<player *>( c );
    if( n != nullptr ) {
        n->deal_damage( nullptr, bodypart_id( "foot_l" ), damage_instance( DT_HEAT, 20 ) );
        n->deal_damage( nullptr, bodypart_id( "foot_r" ), damage_instance( DT_HEAT, 20 ) );
        n->deal_damage( nullptr, bodypart_id( "leg_l" ), damage_instance( DT_HEAT, 20 ) );
        n->deal_damage( nullptr, bodypart_id( "leg_r" ), damage_instance( DT_HEAT, 20 ) );
    } else if( z != nullptr ) {
        if( z->has_effect( effect_ridden ) ) {
            add_msg( m_bad, _( "Your %1$s is burned by the %2$s!" ), z->get_name(), g->m.tr_at( p ).name() );
        }
        // TODO: MATERIALS use fire resistance
        int dam = 30;
        if( z->made_of_any( Creature::cmat_flesh ) ) {
            dam = 50;
        }
        if( z->made_of( material_id( "veggy" ) ) ) {
            dam = 80;
        }
        if( z->made_of( LIQUID ) || z->made_of_any( Creature::cmat_flammable ) ) {
            dam = 200;
        }
        if( z->made_of( material_id( "stone" ) ) ) {
            dam = 15;
        }
        if( z->made_of( material_id( "kevlar" ) ) || z->made_of( material_id( "steel" ) ) ) {
            dam = 5;
        }
        z->deal_damage( nullptr, bodypart_id( "torso" ), damage_instance( DT_HEAT, dam ) );
    }
    c->check_dead_state();
    // Again, you won't be able to remove t_lava with this, but could be used for incendiary traps.
    g->m.tr_at( p ).trigger_aftermath( g->m, p );
    return true;
}

// STUB
bool trapfunc::portal( const tripoint &p, Creature *c, item *i )
{
    // TODO: make this do something unique and interesting
    return telepad( p, c, i );
}

// Don't ask NPCs - they always want to do the first thing that comes to their minds
static bool query_for_item( const player *pl, const itype_id &itemname, const char *que )
{
    return pl->has_amount( itemname, 1 ) && ( !pl->is_player() || query_yn( que ) );
}

static tripoint random_neighbor( tripoint center )
{
    center.x += rng( -1, 1 );
    center.y += rng( -1, 1 );
    return center;
}

static bool sinkhole_safety_roll( player *p, const itype_id &itemname, const int diff )
{
    ///\EFFECT_STR increases chance to attach grapnel, bullwhip, or rope when falling into a sinkhole

    ///\EFFECT_DEX increases chance to attach grapnel, bullwhip, or rope when falling into a sinkhole

    ///\EFFECT_THROW increases chance to attach grapnel, bullwhip, or rope when falling into a sinkhole

    map &here = get_map();

    const int throwing_skill_level = p->get_skill_level( skill_throw );
    const int roll = rng( throwing_skill_level, throwing_skill_level + p->str_cur + p->dex_cur );
    if( roll < diff ) {
        p->add_msg_if_player( m_bad, _( "You fail to attach it…" ) );
        p->use_amount( itemname, 1 );
        here.spawn_item( random_neighbor( p->pos() ), itemname );
        return false;
    }

    std::vector<tripoint> safe;
    for( const tripoint &tmp : g->m.points_in_radius( p->pos(), 1 ) ) {
        if( here.passable( tmp ) && !here.obstructed_by_vehicle_rotation( p->pos(), tmp ) &&
            here.tr_at( tmp ).loadid != tr_pit ) {
            safe.push_back( tmp );
        }
    }
    if( safe.empty() ) {
        p->add_msg_if_player( m_bad, _( "There's nowhere to pull yourself to, and you sink!" ) );
        p->use_amount( itemname, 1 );
        here.spawn_item( random_neighbor( p->pos() ), itemname );
        return false;
    } else {
        p->add_msg_player_or_npc( m_good, _( "You pull yourself to safety!" ),
                                  _( "<npcname> steps on a sinkhole, but manages to pull themselves to safety." ) );
        p->setpos( random_entry( safe ) );
        if( p == &g->u ) {
            g->update_map( *p );
        }

        return true;
    }
}

bool trapfunc::sinkhole( const tripoint &p, Creature *c, item *i )
{
    // tiny creatures don't trigger the sinkhole to collapse
    if( c == nullptr || c->get_size() == creature_size::tiny ) {
        return false;
    }
    monster *z = dynamic_cast<monster *>( c );
    player *pl = dynamic_cast<player *>( c );
    if( z != nullptr ) {
        if( z->has_effect( effect_ridden ) ) {
            add_msg( m_bad, _( "Your %s falls into a sinkhole!" ), z->get_name() );
            g->u.forced_dismount();
        }
    } else if( pl != nullptr ) {
        bool success = false;
        if( query_for_item( pl, itype_grapnel,
                            _( "You step into a sinkhole!  Throw your grappling hook out to try to catch something?" ) ) ) {
            success = sinkhole_safety_roll( pl, itype_grapnel, 6 );
        } else if( query_for_item( pl, itype_bullwhip,
                                   _( "You step into a sinkhole!  Throw your whip out to try and snag something?" ) ) ) {
            success = sinkhole_safety_roll( pl, itype_bullwhip, 8 );
        } else if( query_for_item( pl, itype_rope_30,
                                   _( "You step into a sinkhole!  Throw your rope out to try to catch something?" ) ) ) {
            success = sinkhole_safety_roll( pl, itype_rope_30, 12 );
        } else if( pl->has_trait( trait_WEB_RAPPEL ) && query_yn(
                       _( "You step into a sinkhole!  Throw a web out to try to catch something?" ) ) ) {
            success = sinkhole_safety_roll( pl, itype_rope_30, 3 );
        }

        pl->add_msg_player_or_npc( m_warning, _( "The sinkhole collapses!" ),
                                   _( "A sinkhole under <npcname> collapses!" ) );
        if( success ) {
            // Return early without converting the sinkhole into a pit, because player's position moved when they saved themselves.
            return true;
        }
        pl->add_msg_player_or_npc( m_bad, _( "You fall into the sinkhole!" ),
                                   _( "<npcname> falls into a sinkhole!" ) );
    } else {
        return false;
    }
    // Questionable utility, potentially spawning cover from a concealed pit trap?
    g->m.tr_at( p ).trigger_aftermath( g->m, p );
    g->m.ter_set( p, t_pit );
    c->moves -= 100;
    pit( p, c, i );
    return true;
}

bool trapfunc::ledge( const tripoint &p, Creature *c, item * )
{
    if( c == nullptr ) {
        return false;
    }
    monster *m = dynamic_cast<monster *>( c );
    if( m != nullptr && m->flies() ) {
        return false;
    }
    if( !g->m.has_zlevels() ) {
        if( c == &g->u ) {
            if( !character_funcs::can_fly( get_avatar() ) ) {
                add_msg( m_warning, _( "You fall down a level!" ) );
                g->vertical_move( -1, true );
                if( get_avatar().has_trait( trait_WINGS_BIRD ) || ( one_in( 2 ) &&
                        get_avatar().has_trait( trait_WINGS_BUTTERFLY ) ) ) {
                    add_msg( _( "You flap your wings and flutter down gracefully." ) );
                } else if( get_avatar().has_trait( trait_WEB_RAPPEL ) ) {
                    add_msg( _( "You quickly spin a line of silk and rappel down." ) );
                } else if( get_avatar().has_active_bionic( bio_shock_absorber ) ) {
                    add_msg( m_info,
                             _( "You hit the ground hard, but your shock absorbers handle the impact admirably!" ) );
                } else {
                    get_avatar().impact( 20, p );
                }
            }
        } else {
            c->add_msg_if_npc( _( "<npcname> falls down a level!" ) );
            tripoint dest = c->pos();
            dest.z--;
            c->impact( 20, dest );
            c->setpos( dest );
            if( m != nullptr ) {
                g->despawn_monster( *m );
            }
        }
        // Unlikely you'd want a single-use ledge, but could allow for skylights breaking and spawning glass.
        g->m.tr_at( p ).trigger_aftermath( g->m, p );
        return true;
    }

    int height = 0;
    tripoint where = p;
    tripoint below = where;
    below.z--;
    while( g->m.valid_move( where, below, false, true ) ) {
        where.z--;
        if( g->critter_at( where ) != nullptr ) {
            where.z++;
            break;
        }

        below.z--;
        height++;
    }

    if( height == 0 && c->is_player() ) {
        // For now just special case player, NPCs don't "zedwalk"
        Creature *critter = g->critter_at( below, true );
        if( critter == nullptr || !critter->is_monster() ) {
            return false;
        }

        std::vector<tripoint> valid;
        for( const tripoint &pt : g->m.points_in_radius( below, 1 ) ) {
            if( g->is_empty( pt ) ) {
                valid.push_back( pt );
            }
        }

        if( valid.empty() ) {
            critter->setpos( c->pos() );
            add_msg( m_bad, _( "You fall down under %s!" ), critter->disp_name() );
        } else {
            critter->setpos( random_entry( valid ) );
        }

        height++;
        where.z--;
    } else if( height == 0 ) {
        return false;
    }

    c->add_msg_if_npc( _( "<npcname> falls down a level!" ) );
    player *pl = dynamic_cast<player *>( c );
    if( pl == nullptr ) {
        c->setpos( where );
        c->impact( height * 10, where );
        g->m.tr_at( p ).trigger_aftermath( g->m, p );
        return true;
    }

    if( pl->is_player() ) {
        if( character_funcs::can_fly( *pl->as_character() ) ) {
            return false;
        } else {
            add_msg( m_bad, vgettext( "You fall down %d story!", "You fall down %d stories!", height ),
                     height );
            g->vertical_move( -height, true );
        }
    } else {
        pl->setpos( where );
    }
    if( pl->has_trait( trait_WINGS_BIRD ) || ( one_in( 2 ) &&
            pl->has_trait( trait_WINGS_BUTTERFLY ) ) ) {
        pl->add_msg_player_or_npc( _( "You flap your wings and flutter down gracefully." ),
                                   _( "<npcname> flaps their wings and flutters down gracefully." ) );
    } else if( pl->has_trait( trait_WEB_RAPPEL ) ) {
        pl->add_msg_player_or_npc( _( "You quickly spin a line of silk and rappel down." ),
                                   _( "<npcname> quickly spins a line of silk and rappels down." ) );
    } else if( pl->has_active_bionic( bio_shock_absorber ) ) {
        pl->add_msg_if_player( m_info,
                               _( "You hit the ground hard, but your shock absorbers handle the impact admirably!" ) );
    } else {
        pl->impact( height * 30, where );
    }
    g->m.tr_at( p ).trigger_aftermath( g->m, p );
    return true;
}

bool trapfunc::temple_flood( const tripoint &p, Creature *c, item * )
{
    // Monsters and npcs are completely ignored here, should they?
    if( c == &g->u ) {
        add_msg( m_warning, _( "You step on a loose tile, and water starts to flood the room!" ) );
        tripoint tmp = p;
        int &i = tmp.x;
        int &j = tmp.y;
        for( i = 0; i < MAPSIZE_X; i++ ) {
            for( j = 0; j < MAPSIZE_Y; j++ ) {
                if( g->m.tr_at( tmp ).loadid == tr_temple_flood ) {
                    g->m.remove_trap( tmp );
                }
            }
        }
        g->timed_events.add( TIMED_EVENT_TEMPLE_FLOOD, calendar::turn + 3_turns );
        g->m.tr_at( p ).trigger_aftermath( g->m, p );
        return true;
    }
    return false;
}

bool trapfunc::temple_toggle( const tripoint &p, Creature *c, item * )
{
    // Monsters and npcs are completely ignored here, should they?
    if( c == &g->u ) {
        add_msg( _( "You hear the grinding of shifting rock." ) );
        const ter_id type = g->m.ter( p );
        tripoint tmp = p;
        int &i = tmp.x;
        int &j = tmp.y;
        for( i = 0; i < MAPSIZE_X; i++ ) {
            for( j = 0; j < MAPSIZE_Y; j++ ) {
                if( type == t_floor_red ) {
                    if( g->m.ter( tmp ) == t_rock_green ) {
                        g->m.ter_set( tmp, t_floor_green );
                    } else if( g->m.ter( tmp ) == t_floor_green ) {
                        g->m.ter_set( tmp, t_rock_green );
                    }
                } else if( type == t_floor_green ) {
                    if( g->m.ter( tmp ) == t_rock_blue ) {
                        g->m.ter_set( tmp, t_floor_blue );
                    } else if( g->m.ter( tmp ) == t_floor_blue ) {
                        g->m.ter_set( tmp, t_rock_blue );
                    }
                } else if( type == t_floor_blue ) {
                    if( g->m.ter( tmp ) == t_rock_red ) {
                        g->m.ter_set( tmp, t_floor_red );
                    } else if( g->m.ter( tmp ) == t_floor_red ) {
                        g->m.ter_set( tmp, t_rock_red );
                    }
                }
            }
        }
        g->m.tr_at( p ).trigger_aftermath( g->m, p );
        return true;
    }
    return false;
}

bool trapfunc::glow( const tripoint &p, Creature *c, item * )
{
    if( c == nullptr ) {
        return false;
    }
    monster *z = dynamic_cast<monster *>( c );
    player *n = dynamic_cast<player *>( c );
    if( z != nullptr ) {
        if( one_in( 3 ) ) {
            z->deal_damage( nullptr, bodypart_id( "torso" ), damage_instance( DT_ACID, rng( 5, 10 ) ) );
            z->set_speed_base( z->get_speed_base() * 0.9 );
        }
        if( z->has_effect( effect_ridden ) ) {
            if( one_in( 3 ) ) {
                add_msg( m_bad, _( "You're bathed in radiation!" ) );
                g->u.irradiate( rng( 10, 30 ) );
            } else if( one_in( 4 ) ) {
                add_msg( m_bad, _( "A blinding flash strikes you!" ) );
                explosion_handler::flashbang( p, false, "explosion" );
            } else {
                add_msg( _( "Small flashes surround you." ) );
            }
        }
    }
    if( n != nullptr ) {
        if( one_in( 3 ) ) {
            n->add_msg_if_player( m_bad, _( "You're bathed in radiation!" ) );
            n->irradiate( rng( 10, 30 ) );
        } else if( one_in( 4 ) ) {
            n->add_msg_if_player( m_bad, _( "A blinding flash strikes you!" ) );
            explosion_handler::flashbang( p, false, "explosion" );
        } else {
            c->add_msg_if_player( _( "Small flashes surround you." ) );
        }
    }
    c->check_dead_state();
    g->m.tr_at( p ).trigger_aftermath( g->m, p );
    return true;
}

bool trapfunc::hum( const tripoint &p, Creature *, item * )
{
    int volume = rng( 1, 200 );
    std::string sfx;
    if( volume <= 10 ) {
        //~ a quiet humming sound
        sfx = _( "hrm" );
    } else if( volume <= 50 ) {
        //~ a humming sound
        sfx = _( "hrmmm" );
    } else if( volume <= 100 ) {
        //~ a loud humming sound
        sfx = _( "HRMMM" );
    } else {
        //~ a very loud humming sound
        sfx = _( "VRMMMMMM" );
    }
    sounds::sound( p, volume, sounds::sound_t::activity, sfx, false, "humming", "machinery" );
    g->m.tr_at( p ).trigger_aftermath( g->m, p );
    return true;
}

bool trapfunc::shadow( const tripoint &p, Creature *c, item * )
{
    if( c != &g->u ) {
        return false;
    }
    // Monsters and npcs are completely ignored here, should they?
    for( int tries = 0; tries < 10; tries++ ) {
        tripoint monp = p;
        if( one_in( 2 ) ) {
            monp.x = p.x + rng( -5, +5 );
            monp.y = p.y + ( one_in( 2 ) ? -5 : +5 );
        } else {
            monp.x = p.x + ( one_in( 2 ) ? -5 : +5 );
            monp.y = p.y + rng( -5, +5 );
        }
        if( !g->m.sees( monp, p, 10 ) ) {
            continue;
        }
        if( monster *const spawned = g->place_critter_at( mon_shadow, monp ) ) {
            spawned->add_msg_if_npc( m_warning, _( "A shadow forms nearby." ) );
            spawned->reset_special_rng( "DISAPPEAR" );
            g->m.remove_trap( p );
            break;
        }
    }
    g->m.tr_at( p ).trigger_aftermath( g->m, p );
    return true;
}

// Don't use trigger_aftermath here because no idea what the map might regen into or if it will remove the trap.
bool trapfunc::map_regen( const tripoint &p, Creature *c, item * )
{
    if( c ) {
        player *n = dynamic_cast<player *>( c );
        if( n ) {
            map &here = get_map();
            n->add_msg_if_player( m_warning, _( "Your surroundings shift!" ) );
            tripoint_abs_omt omt_pos = n->global_omt_location();
            const std::string &regen_mapgen = here.tr_at( p ).map_regen_target();
            here.remove_trap( p );
            if( !run_mapgen_update_func( regen_mapgen, omt_pos, nullptr, false ) ) {
                popup( _( "Failed to generate the new map" ) );
                return false;
            }
            here.set_seen_cache_dirty( p );
            here.set_transparency_cache_dirty( p.z );
            return true;
        }
    }
    return false;
}

bool trapfunc::drain( const tripoint &p, Creature *c, item * )
{
    if( c != nullptr ) {
        c->add_msg_if_player( m_bad, _( "You feel your life force sapping away." ) );
        monster *z = dynamic_cast<monster *>( c );
        player *n = dynamic_cast<player *>( c );
        if( n != nullptr ) {
            n->hurtall( 1, nullptr );
        } else if( z != nullptr ) {
            z->deal_damage( nullptr, bodypart_id( "torso" ), damage_instance( DT_TRUE, 1 ) );
        }
        c->check_dead_state();
        g->m.tr_at( p ).trigger_aftermath( g->m, p );
        return true;
    }
    return false;
}

bool trapfunc::cast_spell( const tripoint &p, Creature *critter, item * )
{
    if( critter == nullptr ) {
        return false;
    }
    critter->add_msg_player_or_npc( m_bad, _( "You trigger a %s!" ), _( "<npcname> triggers a %s!" ),
                                    g->m.tr_at( p ).name() );
    const spell trap_spell = g->m.tr_at( p ).spell_data.get_spell( 0 );
    npc dummy;
    trap_spell.cast_all_effects( dummy, critter->pos() );
    trap_spell.make_sound( p );
    g->m.tr_at( p ).trigger_aftermath( g->m, p );
    return true;
}

bool trapfunc::snake( const tripoint &p, Creature *, item * )
{
    //~ the sound a snake makes
    sounds::sound( p, 10, sounds::sound_t::movement, _( "ssssssss" ), false, "misc", "snake_hiss" );
    if( one_in( 6 ) ) {
        g->m.remove_trap( p );
    }
    if( one_in( 3 ) ) {
        for( int tries = 0; tries < 10; tries++ ) {
            tripoint monp = p;
            if( one_in( 2 ) ) {
                monp.x = p.x + rng( -5, +5 );
                monp.y = p.y + ( one_in( 2 ) ? -5 : +5 );
            } else {
                monp.x = p.x + ( one_in( 2 ) ? -5 : +5 );
                monp.y = p.y + rng( -5, +5 );
            }
            if( !g->m.sees( monp, p, 10 ) ) {
                continue;
            }
            if( monster *const spawned = g->place_critter_at( mon_shadow_snake, monp ) ) {
                spawned->add_msg_if_npc( m_warning, _( "A shadowy snake forms nearby." ) );
                spawned->reset_special_rng( "DISAPPEAR" );
                g->m.remove_trap( p );
                break;
            }
        }
    }
    g->m.tr_at( p ).trigger_aftermath( g->m, p );
    return true;
}

/**
 * Takes the name of a trap function and returns a function pointer to it.
 * @param function_name The name of the trapfunc function to find.
 * @return A function object with a pointer to the matched function,
 *         or to trapfunc::none if there is no match.
 */
const trap_function &trap_function_from_string( const std::string &function_name )
{
    static const std::unordered_map<std::string, trap_function> funmap = {{
            { "none", trapfunc::none },
            { "bubble", trapfunc::bubble },
            { "glass", trapfunc::glass },
            { "cot", trapfunc::cot },
            { "beartrap", trapfunc::beartrap },
            { "board", trapfunc::board },
            { "caltrops", trapfunc::caltrops },
            { "caltrops_glass", trapfunc::caltrops_glass },
            { "tripwire", trapfunc::tripwire },
            { "crossbow", trapfunc::crossbow },
            { "shotgun", trapfunc::shotgun },
            { "blade", trapfunc::blade },
            { "snare_light", trapfunc::snare_light },
            { "snare_heavy", trapfunc::snare_heavy },
            { "landmine", trapfunc::landmine },
            { "telepad", trapfunc::telepad },
            { "goo", trapfunc::goo },
            { "dissector", trapfunc::dissector },
            { "sinkhole", trapfunc::sinkhole },
            { "pit", trapfunc::pit },
            { "pit_spikes", trapfunc::pit_spikes },
            { "pit_glass", trapfunc::pit_glass },
            { "lava", trapfunc::lava },
            { "portal", trapfunc::portal },
            { "ledge", trapfunc::ledge },
            { "boobytrap", trapfunc::boobytrap },
            { "temple_flood", trapfunc::temple_flood },
            { "temple_toggle", trapfunc::temple_toggle },
            { "glow", trapfunc::glow },
            { "hum", trapfunc::hum },
            { "shadow", trapfunc::shadow },
            { "map_regen", trapfunc::map_regen },
            { "drain", trapfunc::drain },
            { "spell", trapfunc::cast_spell },
            { "snake", trapfunc::snake }
        }
    };

    const auto iter = funmap.find( function_name );
    if( iter != funmap.end() ) {
        return iter->second;
    }

    debugmsg( "Could not find a trapfunc function matching '%s'!", function_name );
    static const trap_function null_fun = trapfunc::none;
    return null_fun;
}
