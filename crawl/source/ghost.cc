/**
 * @file
 * @brief Player ghost and random Pandemonium demon handling.
**/

#include "AppHdr.h"

#include "ghost.h"

#include <vector>

#include "act-iter.h"
#include "colour.h"
#include "database.h"
#include "enchant-type.h"
#include "env.h"
#include "god-type.h"
#include "item-name.h"
#include "item-prop.h"
#include "jobs.h"
#include "mon-cast.h"
#include "mon-transit.h"
#include "mpr.h"
#include "ng-input.h"
#include "options.h"
#include "skills.h"
#include "spl-util.h"
#include "state.h"
#include "stringutil.h"
#include "tag-version.h"
#include "unwind.h"

#define MAX_GHOST_DAMAGE     50
#define MAX_GHOST_HP        400
#define MAX_GHOST_EVASION    50

// Pan lord AOE conjuration spell list.
static spell_type search_order_aoe_conj[] =
{
    SPELL_SYMBOL_OF_TORMENT,
    SPELL_FIRE_STORM,
    SPELL_GLACIATE,
    SPELL_CHAIN_LIGHTNING,
    SPELL_ORB_OF_ELECTRICITY,
    SPELL_CONJURE_BALL_LIGHTNING,
    SPELL_MARCH_OF_SORROWS,
    SPELL_POISONOUS_CLOUD,
};

// Pan lord conjuration spell list.
static spell_type search_order_conj[] =
{
    SPELL_CALL_DOWN_DAMNATION,
    SPELL_QUICKSILVER_BOLT,
    SPELL_BOLT_OF_DEVASTATION,
    SPELL_IOOD,
    SPELL_BOLT_OF_FIRE,
    SPELL_BOLT_OF_COLD,
    SPELL_BOMBARD,
    SPELL_METAL_SPLINTERS,
    SPELL_LEHUDIBS_CRYSTAL_SPEAR,
    SPELL_LIGHTNING_BOLT,
    SPELL_PLASMA_BEAM,
    SPELL_BOLT_OF_DRAINING,
    SPELL_POISON_ARROW,
    SPELL_CORROSIVE_BOLT,
    SPELL_MINDBURST,
};

// Pan lord self-enchantment spell list.
static spell_type search_order_selfench[] =
{
    SPELL_HASTE,
    SPELL_SILENCE,
    SPELL_INVISIBILITY,
    SPELL_BLINK,
    SPELL_BLINK_RANGE,
};

// Pan lord summoning spell list.
static spell_type search_order_summon[] =
{
    SPELL_HAUNT,
    SPELL_MALIGN_GATEWAY,
    SPELL_SUMMON_DRAGON,
    SPELL_SUMMON_HORRIBLE_THINGS,
    SPELL_SUMMON_EYEBALLS,
    SPELL_SUMMON_VERMIN, // funny
};

// Pan lord misc spell list.
static spell_type search_order_misc[] =
{
    SPELL_DISPEL_UNDEAD_RANGE,
    SPELL_PARALYSE,
    SPELL_VITRIFY,
    SPELL_SLEEP,
    SPELL_MASS_CONFUSION,
    SPELL_DRAIN_MAGIC,
    SPELL_PETRIFY,
    SPELL_POLYMORPH,
    SPELL_SLOW,
    SPELL_SENTINEL_MARK,
    SPELL_DIMENSION_ANCHOR,
    SPELL_FORCE_LANCE,
};

/**
 * A small set of spells that pan lords without (other) spells can make use of.
 * All related to closing the gap with the player, or messing with their
 * movement.
 */
static spell_type search_order_non_spellcaster[] =
{
    SPELL_BLINKBOLT,
    SPELL_BLINK_CLOSE,
    SPELL_HARPOON_SHOT,
};

ghost_demon::ghost_demon()
{
    reset();
}

void ghost_demon::reset()
{
    name.clear();
    species          = SP_UNKNOWN;
    job              = JOB_UNKNOWN;
    religion         = GOD_NO_GOD;
    best_skill       = SK_FIGHTING;
    best_skill_level = 0;
    xl               = 0;
    max_hp           = 0;
    ev               = 0;
    ac               = 0;
    damage           = 0;
    speed            = 10;
    move_energy      = 10;
    see_invis        = false;
    brand            = SPWPN_NORMAL;
    att_type         = AT_HIT;
    att_flav         = AF_PLAIN;
    resists          = 0;
    colour           = COLOUR_UNDEF;
    flies            = false;
    cloud_ring_ench  = ENCH_NONE;
}

#define ADD_SPELL(which_spell) \
    do { \
        const auto spell = (which_spell); \
        if (spell != SPELL_NO_SPELL) \
            spells.emplace_back(spell, 0, MON_SPELL_MAGICAL); \
    } while (0)

static int _panlord_random_resist_level()
{
    return random_choose_weighted(1, -1,
                                  3,  0,
                                  3,  1,
                                  2,  2,
                                  1,  3);
}

static int _panlord_random_elec_resist_level()
{
    return random_choose_weighted(3, 0,
                                  6, 1,
                                  1, 3);
}

/**
 * Generate a random attack_type for a pandemonium_lord. Since this is purely
 * flavour, special attack types are rare.
 */
static attack_type _pan_lord_random_attack_type()
{
    attack_type attack = AT_HIT;
    if (one_chance_in(4))
    {
        do
        {
            // An ugly list, but less brittle and without e.g. false trampling.
            attack = static_cast<attack_type>(random_choose(AT_BITE, AT_STING,
                                      AT_SPORE, AT_TOUCH, AT_PECK, AT_HEADBUTT,
                                      AT_PUNCH, AT_KICK, AT_TENTACLE_SLAP,
                                      AT_TAIL_SLAP, AT_GORE, AT_TRUNK_SLAP));
        }
        while (attack == AT_HIT || !is_plain_attack_type(attack));
    }
    return attack;
}

// this is a bit like a union, but not really
struct attack_form
{
    brand_type brand = SPWPN_NORMAL;
    attack_flavour flavour = AF_PLAIN;
};

static attack_form _brand_attack(brand_type brand)
{
    attack_form form;
    form.brand = brand;
    return form;
}

static attack_form _flavour_attack(attack_flavour flavour)
{
    attack_form form;
    form.flavour = flavour;
    return form;
}

/**
 * Set a random attack type for a pandemonium lord's melee attacks. Some of
 * these are branded attacks, some are custom attack flavours, some are matching
 * attack types & flavours. (Pan lord attack type is randomised, but can be
 * overridden here if required.)
 */
void ghost_demon::set_pan_lord_special_attack()
{
    const attack_form form = random_choose_weighted(
        // Low chance
        10, _brand_attack(SPWPN_VENOM),
        10, _brand_attack(SPWPN_DRAINING),
        4, _flavour_attack(AF_DRAIN_STR),
        4, _flavour_attack(AF_DRAIN_INT),
        2, _flavour_attack(AF_DRAIN_DEX),
        10, _flavour_attack(AF_DROWN),
        // Normal chance
        20, _brand_attack(SPWPN_FLAMING),
        20, _brand_attack(SPWPN_FREEZING),
        20, _brand_attack(SPWPN_ELECTROCUTION),
        20, _brand_attack(SPWPN_VAMPIRISM),
        20, _brand_attack(SPWPN_PAIN),
        20, _flavour_attack(AF_ENSNARE),
        20, _flavour_attack(AF_DRAIN_SPEED),
        20, _flavour_attack(AF_CORRODE),
        20, _flavour_attack(AF_WEAKNESS),
        20, _flavour_attack(AF_DRAG),
        // High chance
        40, _brand_attack(SPWPN_ANTIMAGIC),
        40, _brand_attack(SPWPN_DISTORTION),
        40, _brand_attack(SPWPN_CHAOS),
        40, _flavour_attack(AF_TRAMPLE)
    );

    brand = form.brand;
    if (form.flavour != AF_PLAIN)
        att_flav = form.flavour;

    if (brand == SPWPN_VENOM && coinflip())
        att_type = AT_STING; // such flavour!
    switch (att_flav)
    {
        case AF_TRAMPLE:
            att_type = AT_TRAMPLE;
            break;
        case AF_DROWN:
            att_type = AT_ENGULF;
            break;
        default:
            break;
    }
}

void ghost_demon::set_pan_lord_cloud_ring()
{
    if (brand == SPWPN_ELECTROCUTION)
        cloud_ring_ench = ENCH_RING_OF_THUNDER;
    else if (brand == SPWPN_FLAMING)
        cloud_ring_ench = ENCH_RING_OF_FLAMES;
    else if (brand == SPWPN_CHAOS)
        cloud_ring_ench = ENCH_RING_OF_CHAOS;
    else if (brand == SPWPN_FREEZING)
        cloud_ring_ench = ENCH_RING_OF_ICE;
    else if (att_flav == AF_CORRODE)
        cloud_ring_ench = ENCH_RING_OF_ACID;
    else if (brand == SPWPN_DRAINING)
        cloud_ring_ench = ENCH_RING_OF_MISERY;
    else
    {
        cloud_ring_ench = random_choose_weighted(
            20, ENCH_RING_OF_THUNDER,
            20, ENCH_RING_OF_FLAMES,
            20, ENCH_RING_OF_ICE,
            10, ENCH_RING_OF_MISERY,
             5, ENCH_RING_OF_CHAOS,
             5, ENCH_RING_OF_ACID,
             5, ENCH_RING_OF_MIASMA,
             5, ENCH_RING_OF_MUTATION);
    }
    dprf("This pan lord has a cloud ring ench of %d", cloud_ring_ench);
}

void ghost_demon::init_pandemonium_lord(bool friendly)
{
    do
    {
        name = make_name();
    }
    while (!getLongDescription(name).empty());

    // Is demon a spellcaster?
    // Non-spellcasters always have branded melee and faster/tougher.
    const bool spellcaster = x_chance_in_y(3,4);

    max_hp = 100 + roll_dice(3, 50);

    // Panlord AC/EV should tend to be weighted towards one or the other.
    int total_def = 10 + random2avg(40, 3);
    int split = 1 + biased_random2(4, 2);
    ac = div_rand_round(total_def * split, 10);
    ev = total_def - ac;
    if (coinflip())
        swap(ac, ev);

    see_invis = true;

    resists = 0;
    resists |= mrd(MR_RES_FIRE, _panlord_random_resist_level());
    resists |= mrd(MR_RES_COLD, _panlord_random_resist_level());
    resists |= mrd(MR_RES_ELEC, _panlord_random_elec_resist_level());
    // Demons, like ghosts, automatically get poison res. and life prot.

    // HTH damage:
    damage = 20 + roll_dice(2, 20);

    // Does demon fly?
    flies = x_chance_in_y(2, 3);

    // hit dice:
    xl = 10 + roll_dice(2, 10);

    // Non-spellcasters get upgrades to HD, HP, AC, EV and damage
    if (!spellcaster)
    {
        max_hp = max_hp * 3 / 2;
        ac += 5;
        ev += 5;
        damage += 10;
        xl += 5;
    }

    att_type = _pan_lord_random_attack_type();
    if (one_chance_in(3) || !spellcaster)
        set_pan_lord_special_attack();

    // Spellcasters get a (smaller) chance of a cloud ring below. Friendly pan
    // lords don't get cloud rings so they won't harm the player.
    if (!friendly && !spellcaster && one_chance_in(7))
        set_pan_lord_cloud_ring();

    // Non-casters are fast, casters may get haste.
    if (!spellcaster)
        speed = 11 + roll_dice(2,4);
    else if (one_chance_in(3))
        speed = 10;
    else
        speed = 8 + roll_dice(2,5);

    spells.clear();

    if (spellcaster)
    {
        if (!one_chance_in(10))
            ADD_SPELL(RANDOM_ELEMENT(search_order_conj));

        if (!one_chance_in(10))
        {
            if (coinflip())
            {
                // Demon-summoning should be fairly common.
                if (coinflip())
                {
                    ADD_SPELL(random_choose(SPELL_SUMMON_DEMON,
                                            SPELL_SUMMON_GREATER_DEMON));
                }
                else
                {
                    ADD_SPELL(RANDOM_ELEMENT(search_order_summon));
                    if (coinflip())
                        ADD_SPELL(SPELL_BLINK_ALLIES_ENCIRCLE);
                }
            }
            else
            {
                ADD_SPELL(RANDOM_ELEMENT(search_order_aoe_conj));
                if (!friendly && one_chance_in(7))
                    set_pan_lord_cloud_ring();
            }
        }

        if (coinflip())
            ADD_SPELL(RANDOM_ELEMENT(search_order_selfench));

        if (coinflip())
            ADD_SPELL(RANDOM_ELEMENT(search_order_misc));
    }
    else
    {
        // Non-spellcasters may get one spell
        if (one_chance_in(3))
            ADD_SPELL(RANDOM_ELEMENT(search_order_non_spellcaster));
    }

    if (!spells.empty())
    {
        normalize_spell_freq(spells, spellcaster ? spell_freq_for_hd(xl)
                                                 : spell_freq_for_hd(xl) / 3);
    }

    colour = one_chance_in(10) ? colour_t{ETC_RANDOM} : random_monster_colour();
}

static const set<brand_type> ghost_banned_brands =
                { SPWPN_HOLY_WRATH, SPWPN_CHAOS };

void ghost_demon::init_player_ghost()
{
    // don't preserve transformations for ghosty purposes
    unwind_var<transformation> form(you.form, transformation::none);
    unwind_var<FixedBitVector<NUM_EQUIP>> melded(you.melded,
                                                 FixedBitVector<NUM_EQUIP>());
    unwind_var<bool> fishtail(you.fishtail, false);

    name   = you.your_name;
    max_hp = min(get_real_hp(false, false), MAX_GHOST_HP);
    ev     = min(you.evasion(true), MAX_GHOST_EVASION);
    ac     = you.armour_class();
    dprf("ghost ac: %d, ev: %d", ac, ev);

    see_invis      = you.can_see_invisible();
    resists        = 0;
    set_resist(resists, MR_RES_FIRE, player_res_fire());
    set_resist(resists, MR_RES_COLD, player_res_cold());
    set_resist(resists, MR_RES_ELEC, player_res_electricity());
    // clones might lack innate rPois, copy it. pghosts don't care.
    set_resist(resists, MR_RES_POISON, player_res_poison());
    set_resist(resists, MR_RES_NEG, you.res_negative_energy());
    set_resist(resists, MR_RES_ACID, player_res_acid());
    // multi-level for players, boolean as an innate monster resistance
    set_resist(resists, MR_RES_STEAM, player_res_steam() ? 1 : 0);
    set_resist(resists, MR_RES_MIASMA, you.res_miasma());
    set_resist(resists, MR_RES_PETRIFY, you.res_petrify());

    move_energy = 10;
    speed       = 10;

    damage = 4;
    brand = SPWPN_NORMAL;

    if (you.weapon())
    {
        // This includes ranged weapons, but they're treated as melee.

        const item_def& weapon = *you.weapon();
        if (is_weapon(weapon))
        {
            damage = property(weapon, PWPN_DAMAGE);

            // Bows skill doesn't make bow-bashing better.
            skill_type sk = is_range_weapon(weapon) ? SK_FIGHTING
                                                    : item_attack_skill(weapon);
            damage *= 25 + you.skills[sk];
            damage /= 25;

            if (weapon.base_type == OBJ_WEAPONS)
            {
                brand = static_cast<brand_type>(get_weapon_brand(weapon));

                // normalize banned weapon brands
                if (ghost_banned_brands.count(brand) > 0)
                    brand = SPWPN_NORMAL;

                // Don't copy ranged- or artefact-only brands (reaping etc.).
                if (brand > MAX_GHOST_BRAND)
                    brand = SPWPN_NORMAL;
            }
            else if (weapon.base_type == OBJ_STAVES)
            {
                switch (static_cast<stave_type>(weapon.sub_type))
                {
                // very bad approximations
                case STAFF_FIRE: brand = SPWPN_FLAMING; break;
                case STAFF_COLD: brand = SPWPN_FREEZING; break;
                case STAFF_ALCHEMY: brand = SPWPN_VENOM; break;
                case STAFF_DEATH: brand = SPWPN_PAIN; break;
                case STAFF_AIR: brand = SPWPN_ELECTROCUTION; break;
                case STAFF_EARTH: brand = SPWPN_HEAVY; break;
                default: ;
                }
            }
        }
    }
    else
    {
        // Unarmed combat.
        if (you.has_innate_mutation(MUT_CLAWS))
            damage += you.experience_level;

        damage += you.skills[SK_UNARMED_COMBAT];
    }

    damage *= 30 + you.skills[SK_FIGHTING];
    damage /= 30;

    damage += you.strength() / 4;

    if (damage > MAX_GHOST_DAMAGE)
        damage = MAX_GHOST_DAMAGE;

    species = you.species;
    job = you.char_class;

    religion = you.religion;

    best_skill = ::best_skill(SK_FIRST_SKILL, SK_LAST_SKILL);
    best_skill_level = you.skills[best_skill];
    xl = you.experience_level;

    flies = true;

    add_spells();
}

static colour_t _ugly_thing_assign_colour(colour_t force_colour,
                                          colour_t force_not_colour)
{
    colour_t colour;

    if (force_colour != COLOUR_UNDEF)
        colour = force_colour;
    else
    {
        do
        {
            colour = ugly_thing_random_colour();
        }
        while (force_not_colour != COLOUR_UNDEF && colour == force_not_colour);
    }

    return colour;
}

static attack_flavour _very_ugly_thing_flavour_upgrade(attack_flavour u_att_flav)
{
    switch (u_att_flav)
    {
    case AF_POISON:
        u_att_flav = AF_POISON_STRONG;
        break;

    default:
        break;
    }

    return u_att_flav;
}

static attack_flavour _ugly_thing_colour_to_flavour(colour_t u_colour)
{
    attack_flavour u_att_flav = AF_PLAIN;

    switch (make_low_colour(u_colour))
    {
    case RED:
        u_att_flav = AF_FIRE;
        break;

    case BROWN:
        u_att_flav = AF_ACID;
        break;

    case GREEN:
        u_att_flav = AF_POISON;
        break;

    case CYAN:
        u_att_flav = AF_ELEC;
        break;

    case LIGHTGREY:
        u_att_flav = AF_COLD;
        break;

    default:
        break;
    }

    if (is_high_colour(u_colour))
        u_att_flav = _very_ugly_thing_flavour_upgrade(u_att_flav);

    return u_att_flav;
}

/**
 * Init a ghost demon object corresponding to an ugly thing monster.
 *
 * @param very_ugly     Whether the ugly thing is a very ugly thing.
 * @param only_mutate   Whether to mutate the ugly thing's colour away from its
 *                      old colour (the force_colour).
 * @param force_colour  The ugly thing's colour. (Default COLOUR_UNDEF = random)
 */
void ghost_demon::init_ugly_thing(bool very_ugly, bool only_mutate,
                                  colour_t force_colour)
{
    const monster_type type = very_ugly ? MONS_VERY_UGLY_THING
                                        : MONS_UGLY_THING;
    const monsterentry* stats = get_monster_data(type);

    speed = stats->speed;
    ev = stats->ev;
    ac = stats->AC;
    damage = stats->attack[0].damage;
    move_energy = stats->energy_usage.move;

    // If we're mutating an ugly thing, leave its experience level, hit
    // dice and maximum hit points as they are.
    if (!only_mutate)
    {
        xl = stats->HD;
        max_hp = hit_points(stats->avg_hp_10x);
    }

    const attack_type att_types[] =
    {
        AT_BITE, AT_STING, AT_ENGULF, AT_CLAW, AT_PECK, AT_HEADBUTT, AT_PUNCH,
        AT_KICK, AT_TENTACLE_SLAP, AT_TAIL_SLAP, AT_GORE, AT_TRUNK_SLAP
    };

    att_type = RANDOM_ELEMENT(att_types);

    // An ugly thing always gets a low-intensity colour. If we're
    // mutating it, it always gets a different colour from what it had
    // before.
    colour = _ugly_thing_assign_colour(make_low_colour(force_colour),
                                       only_mutate ? make_low_colour(colour)
                                                   : colour_t{COLOUR_UNDEF});

    // Pick a compatible attack flavour for this colour.
    att_flav = _ugly_thing_colour_to_flavour(colour);
    if (colour == MAGENTA)
        damage = damage * 4 / 3; // +5 for uglies, +9 for v uglies

    // Pick a compatible resistance for this attack flavour.
    ugly_thing_add_resistance(false, att_flav);

    // If this is a very ugly thing, upgrade it properly.
    if (very_ugly)
        ugly_thing_to_very_ugly_thing();
}

void ghost_demon::ugly_thing_to_very_ugly_thing()
{
    // A very ugly thing always gets a high-intensity colour.
    colour = make_high_colour(colour);

    // A very ugly thing sometimes gets an upgraded attack flavour.
    att_flav = _very_ugly_thing_flavour_upgrade(att_flav);

    // Pick a compatible resistance for this attack flavour.
    ugly_thing_add_resistance(true, att_flav);
}

static resists_t _ugly_thing_resists(bool very_ugly, attack_flavour u_att_flav)
{
    switch (u_att_flav)
    {
    case AF_FIRE:
        return MR_RES_FIRE * (very_ugly ? 2 : 1);

    case AF_ACID:
        return MR_RES_ACID;

    case AF_POISON:
    case AF_POISON_STRONG:
        return MR_RES_POISON * (very_ugly ? 2 : 1);

    case AF_ELEC:
        return MR_RES_ELEC * (very_ugly ? 2 : 1);

    case AF_COLD:
        return MR_RES_COLD * (very_ugly ? 2 : 1);

    default:
        return 0;
    }
}

void ghost_demon::ugly_thing_add_resistance(bool very_ugly,
                                            attack_flavour u_att_flav)
{
    resists = _ugly_thing_resists(very_ugly, u_att_flav);
}

void ghost_demon::init_dancing_weapon(const item_def& weapon, int power)
{
    int delay = property(weapon, PWPN_SPEED);
    int damg  = property(weapon, PWPN_DAMAGE);

    if (power > 100)
        power = 100;

    colour = weapon.get_colour();
    flies = true;

    // We want Tukima to reward characters who invest heavily in
    // Hexes skill. Therefore, weapons benefit from very high skill.

    // First set up what the monsters will look like with 100 power.
    // Daggers are weak here! In the table, "44+22" means d44+d22 with
    // d22 being base damage and d44 coming from power.
    // Giant spiked club: speed 12, 44+22 damage, 22 AC, 36 HP, 16 EV
    // Bardiche:          speed 10, 40+20 damage, 18 AC, 40 HP, 15 EV
    // Dagger:            speed 20,  8+ 4 damage,  4 AC, 20 HP, 20 EV
    // Quick blade:       speed 23, 10+ 5 damage,  5 AC, 14 HP, 22 EV
    // Rapier:            speed 18, 14+ 7 damage,  7 AC, 24 HP, 19 EV

    xl = 15;

    speed   = 30 - delay;
    ev      = 25 - delay / 2;
    ac      = damg;
    damage  = 2 * damg;
    max_hp  = delay * 2;

    // Don't allow the speed to become too low.
    speed = max(3, (speed / 2) * (1 + power / 100));

    ev    = max(3, ev * power / 100);
    ac = ac * power / 100;
    max_hp = max(5, max_hp * power / 100);
    damage = max(1, damage * power / 100);
}

void ghost_demon::init_spectral_weapon(const item_def& weapon)
{
    colour = weapon.get_colour();
    flies  = true;
    xl     = 15;
    damage = property(weapon, PWPN_DAMAGE) * 4 / 3;
    speed  = 30;
    ev     = 15;
    ac     = 7;
    max_hp = random_range(20, 30);
}

void ghost_demon::init_inugami_from_player(int power)
{
    const monster_type type = MONS_INUGAMI;
    const monsterentry* stats = get_monster_data(type);

    speed = stats->speed;
    ev = stats->ev;
    ac = stats->AC + div_rand_round(power, 10);
    damage = 5 + div_rand_round(power, 7);
    max_hp = 14 + div_rand_round(power, 4);
    xl = 3 + div_rand_round(power, 15);
    move_energy = stats->energy_usage.move;
    see_invis = true;
}

// Used when creating ghosts: goes through and finds spells for the
// ghost to cast. Death is a traumatic experience, so ghosts only
// remember a few spells.
void ghost_demon::add_spells()
{
    spells.clear();

    for (int i = 0; i < you.spell_no; i++)
    {
        const int chance = max(0, 50 - failure_rate_to_int(raw_spell_fail(you.spells[i])));
        const spell_type spell = translate_spell(you.spells[i]);
        if (spell != SPELL_NO_SPELL
            && !(get_spell_flags(spell) & spflag::no_ghost)
            && is_valid_mon_spell(spell)
            && x_chance_in_y(chance*chance, 50*50))
        {
            spells.emplace_back(spell, 0, MON_SPELL_WIZARD);
        }
    }

    normalize_spell_freq(spells, spell_freq_for_hd(xl));
}

bool ghost_demon::has_spells() const
{
    return spells.size() > 0;
}

// When passed the number for player spells, returns approximate and
// equivalent monster spells. Returns SPELL_NO_SPELL with no equivalent.
spell_type ghost_demon::translate_spell(spell_type spell) const
{
    switch (spell)
    {
#if TAG_MAJOR_VERSION == 34
    case SPELL_CONTROLLED_BLINK:
        return SPELL_BLINK;
#endif
    case SPELL_DRAGON_CALL:
        return SPELL_SUMMON_DRAGON;
    case SPELL_SWIFTNESS:
        return SPELL_SPRINT;
    case SPELL_NECROTISE:
        return SPELL_PAIN;
    case SPELL_CONFUSING_TOUCH:
        return SPELL_CONFUSE;
    case SPELL_CURSE_OF_AGONY:
        return SPELL_AGONY;
    default:
        break;
    }

    return spell;
}

const vector<ghost_demon> ghost_demon::find_ghosts(bool include_player)
{
    vector<ghost_demon> gs;
    if (Options.no_player_bones)
        include_player = false;

    if (include_player && you.undead_state(false) == US_ALIVE)
    {
        ghost_demon player;
        player.init_player_ghost();
        announce_ghost(player);
        gs.push_back(player);
    }

    // Pick up any other ghosts that happen to be on the level if we
    // have space. If the player is undead, add one to the ghost quota
    // for the level.
    find_extra_ghosts(gs);

    return gs;
}

void ghost_demon::find_transiting_ghosts(
    vector<ghost_demon> &gs)
{
    const m_transit_list *mt = get_transit_list(level_id::current());
    if (mt)
    {
        for (auto i = mt->begin(); i != mt->end(); ++i)
        {
            if (i->mons.type == MONS_PLAYER_GHOST)
            {
                const monster& m = i->mons;
                if (m.ghost && !m.props.exists(MIRRORED_GHOST_KEY))
                {
                    announce_ghost(*m.ghost);
                    gs.push_back(*m.ghost);
                }
            }
        }
    }
}

void ghost_demon::announce_ghost(const ghost_demon &g)
{
    UNUSED(g);
#if defined(DEBUG_BONES) || defined(DEBUG_DIAGNOSTICS)
    mprf(MSGCH_DIAGNOSTICS, "Saving ghost: %s", g.name.c_str());
#endif
}

void ghost_demon::find_extra_ghosts(vector<ghost_demon> &gs)
{
    for (monster_iterator mi; mi; ++mi)
    {
        if (mi->type == MONS_PLAYER_GHOST
            && mi->ghost
            && !mi->props.exists(MIRRORED_GHOST_KEY))
        {
            // Bingo!
            announce_ghost(*(mi->ghost));
            gs.push_back(*(mi->ghost));
        }
    }

    // Check the transit list for the current level.
    find_transiting_ghosts(gs);
}

static const set<branch_type> ghosts_nosave =
            { BRANCH_ABYSS, BRANCH_WIZLAB, BRANCH_DESOLATION, BRANCH_TEMPLE,
#if TAG_MAJOR_VERSION == 34
              BRANCH_LABYRINTH,
#endif
            };


/// Is the current location eligible for saving ghosts?
bool ghost_demon::ghost_eligible()
{
    return !crawl_state.game_is_tutorial()
        && (!player_in_branch(BRANCH_DUNGEON) || you.depth > 2)
        && ghosts_nosave.count(you.where_are_you) == 0;
}

bool debug_check_ghost(const ghost_demon &ghost)
{
    // Values greater than the allowed maximum or less then the
    // allowed minimum signalise bugginess.
    if (ghost.damage < 0 || ghost.damage > MAX_GHOST_DAMAGE)
        return false;
    if (ghost.max_hp < 1 || ghost.max_hp > MAX_GHOST_HP)
        return false;
    if (ghost.xl < 1 || ghost.xl > 27)
        return false;
    if (ghost.ev > MAX_GHOST_EVASION)
        return false;
    if (get_resist(ghost.resists, MR_RES_ELEC) < 0)
        return false;
    if (ghost.brand < SPWPN_NORMAL || ghost.brand > MAX_GHOST_BRAND)
        return false;
    if (!species::is_valid(ghost.species))
        return false;
    if (!job_type_valid(ghost.job))
        return false;
    if (ghost.best_skill < SK_FIGHTING || ghost.best_skill >= NUM_SKILLS)
        return false;
    if (ghost.best_skill_level < 0 || ghost.best_skill_level > 27)
        return false;
    if (ghost.religion < GOD_NO_GOD || ghost.religion >= NUM_GODS)
        return false;

    if (ghost.brand == SPWPN_HOLY_WRATH)
        return false;

    // Ghosts don't get non-plain attack types and flavours.
    if (ghost.att_type != AT_HIT || ghost.att_flav != AF_PLAIN)
        return false;

    // Name validation.
    if (!validate_player_name(ghost.name))
        return false;
    // Many combining characters can come per every letter, but if there's
    // that much, it's probably a maliciously forged ghost of some kind.
    if (ghost.name.length() > MAX_NAME_LENGTH * 10 || ghost.name.empty())
        return false;
    if (ghost.name != trimmed_string(ghost.name))
        return false;

    // Check for non-existing spells.
    for (const mon_spell_slot &slot : ghost.spells)
        if (slot.spell < 0 || slot.spell >= NUM_SPELLS)
            return false;

    return true;
}

// Sanity checks for some ghost values.
bool debug_check_ghosts(vector<ghost_demon> &ghosts)
{
    for (const ghost_demon &ghost : ghosts)
        if (!debug_check_ghost(ghost))
            return false;
    return true;
}

int ghost_level_to_rank(const int xl)
{
    if (xl <  4) return 0;
    if (xl <  7) return 1;
    if (xl < 11) return 2;
    if (xl < 16) return 3;
    if (xl < 22) return 4;
    if (xl < 26) return 5;
    if (xl < 27) return 6;
    return 7;
}

// Approximate inverse, in the middle of the range
int ghost_rank_to_level(const int rank)
{
    switch (rank)
    {
    case 0:
        return 2;
    case 1:
        return 5;
    case 2:
        return 9;
    case 3:
        return 13;
    case 4:
        return 19;
    case 5:
        return 24;
    case 6:
        return 26;
    case 7:
        return 27;
    default:
        die("Bad ghost rank %d", rank);
    }
}
