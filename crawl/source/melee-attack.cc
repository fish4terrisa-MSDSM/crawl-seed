/**
 * @file
 * @brief melee_attack class and associated melee_attack methods
 */

#include "AppHdr.h"

#include "melee-attack.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "areas.h"
#include "art-enum.h"
#include "attitude-change.h"
#include "bloodspatter.h"
#include "chardump.h"
#include "cloud.h"
#include "delay.h"
#include "english.h"
#include "env.h"
#include "exercise.h"
#include "fineff.h"
#include "god-conduct.h"
#include "god-item.h"
#include "god-passive.h" // passive_t::convert_orcs
#include "hints.h"
#include "invent.h"
#include "item-prop.h"
#include "mapdef.h"
#include "message.h"
#include "mon-behv.h"
#include "mon-death.h" // maybe_drop_monster_organ
#include "mon-poly.h"
#include "mon-tentacle.h"
#include "religion.h"
#include "shout.h"
#include "spl-damage.h"
#include "spl-goditem.h"
#include "spl-summoning.h" //AF_SPIDER
#include "state.h"
#include "stepdown.h"
#include "stringutil.h"
#include "tag-version.h"
#include "target.h"
#include "terrain.h"
#include "transform.h"
#include "traps.h"
#include "unwind.h"
#include "view.h"
#include "xom.h"

#ifdef NOTE_DEBUG_CHAOS_BRAND
    #define NOTE_DEBUG_CHAOS_EFFECTS
#endif

#ifdef NOTE_DEBUG_CHAOS_EFFECTS
    #include "notes.h"
#endif

/*
 **************************************************
 *             BEGIN PUBLIC FUNCTIONS             *
 **************************************************
*/
melee_attack::melee_attack(actor *attk, actor *defn,
                           int attack_num, int effective_attack_num,
                           bool is_cleaving)
    :  // Call attack's constructor
    ::attack(attk, defn),

    attack_number(attack_num), effective_attack_number(effective_attack_num),
    cleaving(is_cleaving), is_multihit(false),
    is_riposte(false), is_projected(false), charge_pow(0), never_cleave(false),
    wu_jian_attack(WU_JIAN_ATTACK_NONE),
    wu_jian_number_of_targets(1)
{
    attack_occurred = false;
    damage_brand = attacker->damage_brand(attack_number);
    weapon = attacker->weapon(attack_number);
    init_attack(SK_UNARMED_COMBAT, attack_number);
    if (weapon && !using_weapon())
        wpn_skill = SK_FIGHTING;

    attack_position = attacker->pos();
}

bool melee_attack::can_reach()
{
    return attk_type == AT_HIT && weapon && weapon_reach(*weapon) > REACH_NONE
           || flavour_has_reach(attk_flavour)
           || is_projected;
}

bool melee_attack::bad_attempt()
{
    if (!attacker->is_player() || !defender || !defender->is_monster())
        return false;

    if (player_unrand_bad_attempt())
        return true;

    if (!cleave_targets.empty())
    {
        const int range = you.reach_range();
        targeter_cleave hitfunc(attacker, defender->pos(), range);
        return stop_attack_prompt(hitfunc, "attack");
    }

    return stop_attack_prompt(defender->as_monster(), false, attack_position);
}

// Whether this attack, if performed, would prompt the player about damaging
// nearby allies with an unrand property.
bool melee_attack::would_prompt_player()
{
    if (!attacker->is_player())
        return false;

    bool penance;
    return weapon && needs_handle_warning(*weapon, OPER_ATTACK, penance)
           || player_unrand_bad_attempt(true);
}

bool melee_attack::player_unrand_bad_attempt(bool check_only)
{
    // Unrands with secondary effects that can harm nearby friendlies.
    // Don't prompt for confirmation (and leak information about the
    // monster's position) if the player can't see the monster.
    if (!weapon || !you.can_see(*defender))
        return false;

    if (is_unrandom_artefact(*weapon, UNRAND_DEVASTATOR))
    {

        targeter_smite hitfunc(attacker, 1, 1, 1, false);
        hitfunc.set_aim(defender->pos());

        return stop_attack_prompt(hitfunc, "attack",
                                  [](const actor *act)
                                  {
                                      return !god_protects(act->as_monster());
                                  }, nullptr, defender->as_monster(),
                                  check_only);
    }
    else if (is_unrandom_artefact(*weapon, UNRAND_VARIABILITY)
             || is_unrandom_artefact(*weapon, UNRAND_SINGING_SWORD)
                && !silenced(you.pos()))
    {
        targeter_radius hitfunc(&you, LOS_NO_TRANS);

        return stop_attack_prompt(hitfunc, "attack",
                               [](const actor *act)
                               {
                                   return !god_protects(act->as_monster());
                               }, nullptr, defender->as_monster(),
                               check_only);
    }
    if (is_unrandom_artefact(*weapon, UNRAND_TORMENT))
    {
        targeter_radius hitfunc(&you, LOS_NO_TRANS);

        return stop_attack_prompt(hitfunc, "attack",
                               [] (const actor *m)
                               {
                                   return !m->res_torment()
                                       && !god_protects(m->as_monster());
                               },
                                  nullptr, defender->as_monster(),
                                check_only);
    }
    if (is_unrandom_artefact(*weapon, UNRAND_ARC_BLADE))
    {
        vector<const actor *> exclude;
        return !safe_discharge(defender->pos(), exclude, check_only);
    }
    if (is_unrandom_artefact(*weapon, UNRAND_POWER))
    {
        targeter_beam hitfunc(&you, 4, ZAP_SWORD_BEAM, 100, 0, 0);
        hitfunc.beam.aimed_at_spot = false;
        hitfunc.set_aim(defender->pos());

        return stop_attack_prompt(hitfunc, "attack",
                               [](const actor *act)
                               {
                                   return !god_protects(act->as_monster());
                               }, nullptr, defender->as_monster(),
                               check_only);
    }
    return false;
}

bool melee_attack::handle_phase_attempted()
{
    // Skip invalid and dummy attacks.
    if (defender && (!adjacent(attack_position, defender->pos())
                     && !can_reach())
        || attk_flavour == AF_CRUSH
           && (!attacker->can_constrict(*defender, CONSTRICT_MELEE)
               || attacker->is_monster() && attacker->mid == MID_PLAYER))
    {
        --effective_attack_number;

        return false;
    }

    if (bad_attempt())
    {
        cancel_attack = true;
        return false;
    }

    if (attacker->is_player())
    {
        // Set delay now that we know the attack won't be cancelled.
        if (!is_riposte && !is_multihit && !cleaving
            && wu_jian_attack == WU_JIAN_ATTACK_NONE)
        {
            you.time_taken = you.attack_delay().roll();
        }

        const caction_type cact_typ = is_riposte ? CACT_RIPOSTE : CACT_MELEE;
        if (weapon)
        {
            if (weapon->base_type == OBJ_WEAPONS)
                if (is_unrandom_artefact(*weapon)
                    && get_unrand_entry(weapon->unrand_idx)->type_name)
                {
                    count_action(cact_typ, weapon->unrand_idx);
                }
                else
                    count_action(cact_typ, weapon->sub_type);
            else if (weapon->base_type == OBJ_STAVES)
                count_action(cact_typ, WPN_STAFF);
        }
        else
            count_action(cact_typ, -1, -1); // unarmed subtype/auxtype
    }
    else
    {
        // Only the first attack costs any energy.
        if (!effective_attack_number)
        {
            int energy = attacker->as_monster()->action_energy(EUT_ATTACK);
            int delay = attacker->attack_delay().roll();
            dprf(DIAG_COMBAT, "Attack delay %d, multiplier %1.1f", delay, energy * 0.1);
            ASSERT(energy > 0);
            ASSERT(delay > 0);

            attacker->as_monster()->speed_increment
                -= div_rand_round(energy * delay, 10);
        }

        // Statues and other special monsters which have AT_NONE need to lose
        // energy, but otherwise should exit the melee attack now.
        if (attk_type == AT_NONE)
            return false;
    }

    if (attacker != defender && !is_riposte)
    {
        // Allow setting of your allies' target, etc.
        attacker->attacking(defender);

        check_autoberserk();
    }

    // Xom thinks fumbles are funny...
    if (attacker->fumbles_attack())
    {
        // ... and thinks fumbling when trying to hit yourself is just
        // hilarious.
        xom_is_stimulated(attacker == defender ? 200 : 10);
        return false;
    }
    // Non-fumbled self-attacks due to confusion are still pretty funny, though.
    else if (attacker == defender && attacker->confused())
        xom_is_stimulated(100);

    // Any attack against a monster we're afraid of has a chance to fail
    if (attacker->is_player() && defender &&
        you.afraid_of(defender->as_monster()) && one_chance_in(3))
    {
        mprf("You attempt to attack %s, but flinch away in fear!",
             defender->name(DESC_THE).c_str());
        return false;
    }

    if (attk_flavour == AF_SHADOWSTAB
        && defender && !defender->can_see(*attacker))
    {
        if (you.see_cell(attack_position))
        {
            mprf("%s strikes at %s from the darkness!",
                 attacker->name(DESC_THE, true).c_str(),
                 defender->name(DESC_THE).c_str());
        }
        to_hit = AUTOMATIC_HIT;
        needs_message = false;
    }
    else if (attacker->is_monster()
             && attacker->type == MONS_DROWNED_SOUL)
    {
        to_hit = AUTOMATIC_HIT;
    }

    attack_occurred = true;

    // Check for player practicing dodging
    if (defender && defender->is_player())
        practise_being_attacked();

    return true;
}

bool melee_attack::handle_phase_dodged()
{
    did_hit = false;

    if (needs_message)
    {
        // TODO: Unify these, placed player_warn_miss here so I can remove
        // player_attack
        if (attacker->is_player())
            player_warn_miss();
        else
        {
            mprf("%s%s misses %s%s",
                 atk_name(DESC_THE).c_str(),
                 evasion_margin_adverb().c_str(),
                 defender_name(true).c_str(),
                 attack_strength_punctuation(damage_done).c_str());
        }
    }

    if (attacker->is_player())
    {
        // Upset only non-sleeping non-fleeing monsters if we missed.
        if (!defender->asleep() && !mons_is_fleeing(*defender->as_monster()))
            behaviour_event(defender->as_monster(), ME_WHACK, attacker);
    }

    if (defender->is_player())
        count_action(CACT_DODGE, DODGE_EVASION);

    maybe_trigger_jinxbite();

    if (attacker != defender
        && attacker->alive() && defender->can_see(*attacker)
        && !defender->cannot_act() && !defender->confused()
        && (!defender->is_player() || !attacker->as_monster()->neutral())
        && !mons_aligned(attacker, defender) // confused friendlies attacking
        // Retaliation only works on the first attack in a round.
        // FIXME: player's attack is -1, even for auxes
        && effective_attack_number <= 0)
    {
        if (adjacent(defender->pos(), attack_position))
        {
            if (defender->is_player()
                ? you.has_mutation(MUT_REFLEXIVE_HEADBUTT)
                : mons_species(mons_base_type(*defender->as_monster()))
                == MONS_MINOTAUR)
            {
                do_minotaur_retaliation();
            }

            // Retaliations can kill!
            if (!attacker->alive())
                return false;

            if (defender->is_player() && player_equip_unrand(UNRAND_STARLIGHT))
                do_starlight();
        }

        maybe_riposte();
        // Retaliations can kill!
        if (!attacker->alive())
            return false;
    }

    return true;
}

void melee_attack::maybe_riposte()
{
    // only riposte via fencer's gloves, which (I take it from this code)
    // monsters can't use
    const bool using_fencers =
                defender->is_player()
                    && player_equip_unrand(UNRAND_FENCERS)
                    && (!defender->weapon()
                        || is_melee_weapon(*defender->weapon()));
    if (using_fencers
        && !is_riposte // no ping-pong!
        && one_chance_in(3)
        && you.reach_range() >= grid_distance(you.pos(), attack_position))
    {
        riposte();
    }
}

void melee_attack::apply_black_mark_effects()
{
    enum black_mark_effect
    {
        ANTIMAGIC,
        WEAKNESS,
        DRAINING,
    };

    // Less reliable effects for players.
    if (attacker->is_player()
        && you.has_mutation(MUT_BLACK_MARK)
        && one_chance_in(5)
        || attacker->is_monster()
           && attacker->as_monster()->has_ench(ENCH_BLACK_MARK))
    {
        if (!defender->alive())
            return;

        vector<black_mark_effect> effects;

        if (defender->antimagic_susceptible())
            effects.push_back(ANTIMAGIC);
        if (defender->is_player()
            || mons_has_attacks(*defender->as_monster()))
        {
            effects.push_back(WEAKNESS);
        }
        if (defender->res_negative_energy() < 3)
            effects.push_back(DRAINING);

        if (effects.empty())
            return;

        black_mark_effect choice = effects[random2(effects.size())];

        switch (choice)
        {
            case ANTIMAGIC:
                antimagic_affects_defender(damage_done * 8);
                break;
            case WEAKNESS:
                defender->weaken(attacker, 6);
                break;
            case DRAINING:
                defender->drain(attacker, false, damage_done);
                break;
        }
    }
}

void melee_attack::do_ooze_engulf()
{
    if (attacker->is_player()
        && you.has_mutation(MUT_ENGULF)
        && defender->alive()
        && !defender->as_monster()->has_ench(ENCH_WATER_HOLD)
        && attacker->can_engulf(*defender)
        && coinflip())
    {
        defender->as_monster()->add_ench(mon_enchant(ENCH_WATER_HOLD, 1,
                                                     attacker, 1));
        mprf("You engulf %s in ooze!", defender->name(DESC_THE).c_str());
        // Smothers sticky flame.
        defender->expose_to_element(BEAM_WATER, 0);
    }
}


static void _apply_flux_contam(monster &m)
{
    const mon_enchant old_glow = m.get_ench(ENCH_CONTAM);

    if (old_glow.degree >= 2)
    {
        const int max_dam = get_form()->contam_dam();
        const int dam = random2(max_dam);
        string msg = make_stringf(" shudders as magic cascades through %s%s",
                                  m.pronoun(PRONOUN_OBJECTIVE).c_str(),
                                  attack_strength_punctuation(dam).c_str());
        dprf("done %d (max %d)", dam, max_dam);
        simple_monster_message(m, msg.c_str());
        if (dam)
        {
            m.hurt(&you, dam, BEAM_MMISSILE, KILLED_BY_BEAM /*eh*/);
            if (!m.alive())
                return;
        }
        m.malmutate("");
        m.del_ench(ENCH_CONTAM, true);
        return;
    }

    m.add_ench(mon_enchant(ENCH_CONTAM, 1, &you));
    if (!old_glow.degree)
        simple_monster_message(m, " begins to glow.");
    else
        simple_monster_message(m, " glows dangerously bright.");
}

/* An attack has been determined to have hit something
 *
 * Handles to-hit effects for both attackers and defenders,
 * Determines damage and passes off execution to handle_phase_damaged
 * Also applies weapon brands
 *
 * Returns true if combat should continue, false if it should end here.
 */
bool melee_attack::handle_phase_hit()
{
    did_hit = true;
    perceived_attack = true;
    bool hit_woke_orc = false;

    if (attacker->is_player())
    {
        if (crawl_state.game_is_hints())
            Hints.hints_melee_counter++;

        // TODO: Remove this (placed here so I can get rid of player_attack)
        if (have_passive(passive_t::convert_orcs)
            && mons_genus(defender->mons_species()) == MONS_ORC
            && !defender->is_summoned()
            && !defender->as_monster()->is_shapeshifter()
            && you.see_cell(defender->pos()) && defender->asleep())
        {
            hit_woke_orc = true;
        }
    }

    damage_done = 0;
    // Slimify does no damage and serves as an on-hit effect, handle it
    if (attacker->is_player() && you.duration[DUR_SLIMIFY]
        && mon_can_be_slimified(defender->as_monster())
        && !cleaving)
    {
        // Bail out after sliming so we don't get aux unarmed and
        // attack a fellow slime.
        slimify_monster(defender->as_monster());
        you.duration[DUR_SLIMIFY] = 0;

        return false;
    }

    // This does more than just calculate the damage, it also sets up
    // messages, etc. It also wakes nearby creatures on a failed stab,
    // meaning it could have made the attacked creature vanish. That
    // will be checked early in player_monattack_hit_effects
    damage_done += calc_damage();

    // Calculate and apply infusion costs immediately after we calculate
    // damage, so that later events don't result in us skipping the cost.
    if (attacker->is_player())
    {
        const int infusion = you.infusion_amount();
        if (infusion)
        {
            pay_mp(infusion);
            finalize_mp_cost();
        }
    }

    bool stop_hit = false;
    // Check if some hit-effect killed the monster.
    if (attacker->is_player())
        stop_hit = !player_monattk_hit_effects();

    // check_unrand_effects is safe to call with a dead defender, so always
    // call it, even if the hit effects said to stop.
    if (stop_hit)
    {
        check_unrand_effects();
        return false;
    }

    if (damage_done > 0 || flavour_triggers_damageless(attk_flavour))
    {
        if (!handle_phase_damaged())
            return false;

        // TODO: Remove this, (placed here to remove player_attack)
        if (attacker->is_player() && hit_woke_orc)
        {
            // Call function of orcs first noticing you, but with
            // beaten-up conversion messages (if applicable).
            beogh_follower_convert(defender->as_monster(), true);
        }
    }
    else
    {
        if (needs_message)
        {
            attack_verb = attacker->is_player()
                                    ? attack_verb
                                    : attacker->conj_verb(mons_attack_verb());

            // TODO: Clean this up if possible, checking atype for do / does is ugly
            mprf("%s %s %s but %s no damage.",
                attacker->name(DESC_THE).c_str(),
                attack_verb.c_str(),
                defender_name(true).c_str(),
                attacker->is_player() ? "do" : "does");
        }
    }

    maybe_trigger_jinxbite();

    // Check for weapon brand & inflict that damage too
    apply_damage_brand();

    // Apply flux form's sfx.
    if (attacker->is_player() && you.form == transformation::flux
        && defender->alive() && defender->is_monster())
    {
        _apply_flux_contam(*(defender->as_monster()));
    }

    // Fireworks when using Serpent's Lash to kill.
    if (!defender->alive()
        && defender->as_monster()->can_bleed()
        && wu_jian_has_momentum(wu_jian_attack))
    {
        blood_spray(defender->pos(), defender->as_monster()->type,
                    damage_done / 5);
        defender->as_monster()->flags |= MF_EXPLODE_KILL;
    }

    // Trigger Curse of Agony after most normal damage is already applied
    if (attacker->is_player() && defender->alive() && defender->is_monster()
        && defender->as_monster()->has_ench(ENCH_CURSE_OF_AGONY))
    {
        mon_enchant agony = defender->as_monster()->get_ench(ENCH_CURSE_OF_AGONY);
        torment_cell(defender->pos(), &you, TORMENT_AGONY);
        agony.degree -= 1;

        if (agony.degree == 0)
            defender->as_monster()->del_ench(ENCH_CURSE_OF_AGONY);
        else
            defender->as_monster()->update_ench(agony);
    }

    if (check_unrand_effects())
        return false;

    if (damage_done > 0)
    {
        apply_black_mark_effects();
        do_ooze_engulf();
    }

    if (attacker->is_player())
    {
        // Always upset monster regardless of damage.
        // However, successful stabs inhibit shouting.
        behaviour_event(defender->as_monster(), ME_WHACK, attacker,
                        coord_def(), !stab_attempt);

        // [ds] Monster may disappear after behaviour event.
        if (!defender->alive())
            return true;
    }
    else if (defender->is_player())
    {
        // These effects (mutations right now) are only triggered when
        // the player is hit, each of them will verify their own required
        // parameters.
        do_passive_freeze();
        do_fiery_armour_burn();
        emit_foul_stench();
    }

    return true;
}

static void _inflict_deathly_blight(monster &m)
{
    if (m.holiness() & MH_NONLIVING
        || mons_is_conjured(m.type)
        || mons_is_tentacle_or_tentacle_segment(m.type))
    {
        return;
    }

    const int dur = random_range(3, 6) * BASELINE_DELAY;
    bool worked = false;
    if (!m.stasis() && !m.is_stationary())
        worked = m.add_ench(mon_enchant(ENCH_SLOW, 0, &you, dur)) || worked;
    if (mons_has_attacks(m))
        worked = m.add_ench(mon_enchant(ENCH_WEAK, 1, &you, dur)) || worked;
    if (m.holiness() & (MH_NATURAL | MH_PLANT))
        worked = m.add_ench(mon_enchant(ENCH_DRAINED, 2, &you, dur)) || worked;
    if (worked && you.can_see(m))
        simple_monster_message(m, " decays.");
}

bool melee_attack::handle_phase_damaged()
{
    if (!attack::handle_phase_damaged())
        return false;

    if (attacker->is_player())
    {
        if (player_equip_unrand(UNRAND_POWER_GLOVES))
            inc_mp(div_rand_round(damage_done, 8));
        if (you.form == transformation::death && defender->alive()
            && defender->is_monster())
        {
            _inflict_deathly_blight(*(defender->as_monster()));
        }
    }

    return true;
}

bool melee_attack::handle_phase_aux()
{
    if (attacker->is_player()
        && !cleaving
        && wu_jian_attack != WU_JIAN_ATTACK_TRIGGERED_AUX
        && !is_projected)
    {
        // returns whether an aux attack successfully took place
        // additional attacks from cleave don't get aux
        if (!defender->as_monster()->friendly()
            && adjacent(defender->pos(), attack_position))
        {
            player_aux_unarmed();
        }

        // Don't print wounds after the first attack with quick blades.
        if (!weapon_multihits(weapon))
            print_wounds(*defender->as_monster());
    }

    return true;
}

/**
 * Devour a monster whole!
 *
 * @param defender  The monster in question.
 */
static void _devour(monster &victim)
{
    // Sometimes, one's eyes are larger than one's stomach-mouth.
    const int size_delta = victim.body_size(PSIZE_BODY)
                            - you.body_size(PSIZE_BODY);
    mprf("You devour %s%s!",
         size_delta <= 0 ? "" :
         size_delta <= 1 ? "half of " :
                           "a chunk of ",
         victim.name(DESC_THE).c_str());

    // give a clearer message for eating invisible things
    if (!you.can_see(victim))
    {
        mprf("It tastes like %s.",
             mons_type_name(mons_genus(victim.type), DESC_PLAIN).c_str());
        // this could be the actual creature name, but it feels more
        // 'flavourful' this way??
    }
    if (victim.has_ench(ENCH_STICKY_FLAME))
        mprf("Spicy!");

    // Devour the corpse.
    victim.props[NEVER_CORPSE_KEY] = true;

    // ... but still drop dragon scales, etc, if appropriate.
    monster_type orig = victim.type;
    if (victim.props.exists(ORIGINAL_TYPE_KEY))
        orig = (monster_type) victim.props[ORIGINAL_TYPE_KEY].get_int();
    maybe_drop_monster_organ(victim.type, orig, victim.pos());

    // Healing.
    if (you.duration[DUR_DEATHS_DOOR])
        return;

    const int xl = victim.get_experience_level();
    const int xl_heal = xl + random2(xl);
    const int scale = 100;
    const int form_lvl = get_form()->get_level(scale);
    const int form_heal = div_rand_round(form_lvl, scale) + random2(20); // max 40
    const int healing = 1 + min(xl_heal, form_heal);
    dprf("healing for %d", healing);

    you.heal(healing);
    calc_hp();
    canned_msg(MSG_GAIN_HEALTH);
}


/**
 * Possibly devour the defender whole.
 *
 * @param defender  The defender in question.
 */
static void _consider_devouring(monster &defender)
{
    ASSERT(!crawl_state.game_is_arena());

    // Don't eat orcs, even heretics might be worth a miracle
    if (you_worship(GOD_BEOGH)
        && mons_genus(mons_species(defender.type)) == MONS_ORC)
    {
        return;
    }

    // can't eat enemies that leave no corpses...
    if (!mons_class_can_leave_corpse(mons_species(defender.type))
        || defender.is_summoned()
        || defender.flags & MF_HARD_RESET
        // the curse of midas...
        || have_passive(passive_t::goldify_corpses))
    {
        return;
    }

    // shapeshifters are mutagenic.
    // there's no real point to this now, but it is funny.
    // should polymorphed enemies do the same?
    if (defender.is_shapeshifter())
    {
        // handle this carefully, so the player knows what's going on
        mprf("You spit out %s as %s %s & %s in your maw!",
             defender.name(DESC_THE).c_str(),
             defender.pronoun(PRONOUN_SUBJECTIVE).c_str(),
             conjugate_verb("twist", defender.pronoun_plurality()).c_str(),
             conjugate_verb("change", defender.pronoun_plurality()).c_str());
        return;
    }

    if (one_chance_in(3))
        return;

    // chow down.
    _devour(defender);
}

/**
 * Handle effects that fire when the defender (the target of the attack) is
 * killed.
 *
 * @return  Not sure; it seems to never be checked & always be true?
 */
bool melee_attack::handle_phase_killed()
{
    if (attacker->is_player()
        && you.form == transformation::maw
        && defender->is_monster() // better safe than sorry
        && defender->type != MONS_NO_MONSTER // already reset
        && adjacent(defender->pos(), attack_position))
    {
        _consider_devouring(*defender->as_monster());
    }

    // Wyrmbane needs to be notified of deaths, including ones due to aux
    // attacks, but other users of melee_effects() don't want to possibly
    // be called twice. Adding another entry for a single artefact would
    // be overkill, so here we call it by hand. check_unrand_effects()
    // avoided triggering Wyrmbane's death effect earlier in the attack.
    if (unrand_entry && weapon && weapon->unrand_idx == UNRAND_WYRMBANE)
    {
        unrand_entry->melee_effects(weapon, attacker, defender,
                                               true, special_damage);
    }

    return attack::handle_phase_killed();
}

static void _handle_spectral_brand(actor &attacker, const actor &defender)
{
    if (attacker.type == MONS_SPECTRAL_WEAPON || !defender.alive())
        return;
    attacker.triggered_spectral = true;
    spectral_weapon_fineff::schedule(attacker, defender);
}

bool melee_attack::handle_phase_end()
{
    if (!is_multihit && weapon_multihits(weapon))
    {
        const int hits_per_targ = weapon_hits_per_swing(*weapon);
        list<actor*> extra_hits;
        for (int i = 1; i < hits_per_targ; i++)
            extra_hits.push_back(defender);
        // effective_attack_number will be wrong for a monster that
        // does a cleaving multi-hit attack. God help us.
        attack_multiple_targets(*attacker, extra_hits, attack_number,
                                effective_attack_number, wu_jian_attack,
                                is_projected, false);
        if (attacker->is_player())
            print_wounds(*defender->as_monster());
    }

    if (!cleave_targets.empty() && !simu
        // WJC AOEs mayn't cleave.
        && wu_jian_attack != WU_JIAN_ATTACK_WHIRLWIND
        && wu_jian_attack != WU_JIAN_ATTACK_WALL_JUMP
        && wu_jian_attack != WU_JIAN_ATTACK_TRIGGERED_AUX)
    {
        attack_multiple_targets(*attacker, cleave_targets, attack_number,
                              effective_attack_number, wu_jian_attack,
                              is_projected, true);
    }

    // Check for passive mutation effects.
    if (defender->is_player() && defender->alive() && attacker != defender)
    {
        mons_do_eyeball_confusion();
        mons_do_tendril_disarm();
    }

    if (attacker->alive() && attacker->is_monster())
    {
        monster* mon_attacker = attacker->as_monster();

        if (mon_attacker->has_ench(ENCH_ROLLING))
            mon_attacker->del_ench(ENCH_ROLLING);

        // Blazeheart golems tear themselves apart on impact
        if (mon_attacker->type == MONS_BLAZEHEART_GOLEM && did_hit)
        {
            mon_attacker->hurt(mon_attacker,
                               mon_attacker->max_hit_points / 3 + 1,
                               BEAM_MISSILE);
        }
    }

    if (defender && !is_multihit)
    {
        if (damage_brand == SPWPN_SPECTRAL)
            _handle_spectral_brand(*attacker, *defender);
        // Use the Nessos hack to give the player glaive of the guard spectral too
        if (weapon && is_unrandom_artefact(*weapon, UNRAND_GUARD))
            _handle_spectral_brand(*attacker, *defender);
    }

    // Dead but not yet reset, most likely due to an attack flavour that
    // destroys the attacker on-hit.
    if (attacker->is_monster()
        && attacker->as_monster()->type != MONS_NO_MONSTER
        && attacker->as_monster()->hit_points < 1)
    {
        monster_die(*attacker->as_monster(), KILL_MISC, NON_MONSTER);
    }

    return attack::handle_phase_end();
}

/* Initiate the processing of the attack
 *
 * Called from the main code (fight.cc), this method begins the actual combat
 * for a particular attack and is responsible for looping through each of the
 * appropriate phases (which then can call other related phases within
 * themselves).
 *
 * Returns whether combat was completely successful
 *      If combat was not successful, it could be any number of reasons, like
 *      the defender or attacker dying during the attack? or a defender moving
 *      from its starting position.
 */
bool melee_attack::attack()
{
    if (!cleaving && !never_cleave)
    {
        cleave_setup();
        if (!handle_phase_attempted())
            return false;

        // If we're a monster that was supposed to get a free instant cleave
        // attack, refund the energy now. (It may look strange that this is
        // in the '!cleaving' block, but otherwise the 'free' attack will only
        // ever happen if there were multiple targets being hit by it.)
        if (attacker->is_monster())
        {
            monster* mons = attacker->as_monster();
            if (mons->has_ench(ENCH_INSTANT_CLEAVE))
            {
                mons->del_ench(ENCH_INSTANT_CLEAVE);
                mons->speed_increment += mons->action_energy(EUT_ATTACK);
            }
        }
    }

    if (attacker != defender && attacker->is_monster()
        && mons_self_destructs(*attacker->as_monster()))
    {
        attacker->self_destruct();
        return did_hit = perceived_attack = true;
    }

    string saved_gyre_name;
    if (weapon && is_unrandom_artefact(*weapon, UNRAND_GYRE))
    {
        saved_gyre_name = get_artefact_name(*weapon);
        const bool gimble = effective_attack_number % 2;
        set_artefact_name(*weapon, gimble ? "quick blade \"Gimble\""
                                          : "quick blade \"Gyre\"");
    }

    // Restore gyre's name before we return. We cannot use an unwind_var here
    // because the precise address of the ARTEFACT_NAME_KEY property might
    // change, for example if a summoned item is reset.
    ON_UNWIND
    {
        if (!saved_gyre_name.empty() && weapon
                && is_unrandom_artefact(*weapon, UNRAND_GYRE))
        {
            set_artefact_name(*weapon, saved_gyre_name);
        }
    };

    // Attacker might have died from effects of cleaving handled prior to this
    if (!attacker->alive())
        return false;

    // We might have killed the kraken target by cleaving a tentacle.
    if (!defender->alive())
    {
        handle_phase_killed();
        handle_phase_end();
        return attack_occurred;
    }

    // Apparently I'm insane for believing that we can still stay general past
    // this point in the combat code, mebe I am! --Cryptic

    // Calculate various ev values and begin to check them to determine the
    // correct handle_phase_ handler.
    const int ev = defender->evasion(false, attacker);
    ev_margin = test_hit(to_hit, ev, !attacker->is_player());
    bool shield_blocked = attack_shield_blocked(true);

    // Stuff for god conduct, this has to remain here for scope reasons.
    god_conduct_trigger conducts[3];

    if (attacker->is_player() && attacker != defender)
    {
        set_attack_conducts(conducts, *defender->as_monster(),
                            you.can_see(*defender));

        if (player_under_penance(GOD_ELYVILON)
            && god_hates_your_god(GOD_ELYVILON)
            && ev_margin >= 0
            && one_chance_in(20))
        {
            simple_god_message(" blocks your attack.", GOD_ELYVILON);
            handle_phase_end();
            return false;
        }
        // Check for stab (and set stab_attempt and stab_bonus)
        player_stab_check();
        // Make sure we hit if we passed the stab check.
        if (stab_attempt && stab_bonus > 0)
        {
            ev_margin = AUTOMATIC_HIT;
            shield_blocked = false;
        }

        // Serpent's Lash does not miss
        if (wu_jian_has_momentum(wu_jian_attack))
           ev_margin = AUTOMATIC_HIT;
    }

    if (shield_blocked)
    {
        handle_phase_blocked();
        maybe_riposte();
        if (!attacker->alive())
        {
            handle_phase_end();
            return false;
        }
    }
    else
    {
        if (attacker != defender
            && (adjacent(defender->pos(), attack_position) || is_projected)
            && !is_riposte)
        {
            // Check for defender Spines
            do_spines();

            // Spines can kill! With Usk's pain bond, they can even kill the
            // defender.
            if (!attacker->alive() || !defender->alive())
                return false;
        }

        if (ev_margin >= 0)
        {
            bool cont = handle_phase_hit();

            if (cont)
                attacker_sustain_passive_damage();
            else
            {
                if (!defender->alive())
                    handle_phase_killed();
                handle_phase_end();
                return false;
            }
        }
        else
            handle_phase_dodged();
    }

    // don't crash on banishment
    if (!defender->pos().origin())
        handle_noise(defender->pos());

    // Noisy weapons.
    if (attacker->is_player()
        && weapon
        && is_artefact(*weapon)
        && artefact_property(*weapon, ARTP_NOISE))
    {
        noisy_equipment();
    }

    alert_defender();

    if (!defender->alive())
        handle_phase_killed();

    handle_phase_aux();

    handle_phase_end();

    return attack_occurred;
}

void melee_attack::check_autoberserk()
{
    if (defender->is_monster() && mons_is_firewood(*defender->as_monster()))
        return;

    if (x_chance_in_y(attacker->angry(), 100))
    {
        attacker->go_berserk(false);
        return;
    }
}

bool melee_attack::check_unrand_effects()
{
    if (unrand_entry && unrand_entry->melee_effects && weapon)
    {
        const bool died = !defender->alive();

        // Don't trigger the Wyrmbane death effect yet; that is done in
        // handle_phase_killed().
        if (weapon->unrand_idx == UNRAND_WYRMBANE && died)
            return true;

        // Recent merge added damage_done to this method call
        unrand_entry->melee_effects(weapon, attacker, defender,
                                    died, damage_done);
        return !defender->alive(); // may have changed
    }

    return false;
}

class AuxAttackType
{
public:
    AuxAttackType(int _damage, int _chance, string _name) :
    damage(_damage), chance(_chance), name(_name) { };
public:
    virtual int get_damage(bool /*random*/) const { return damage; };
    virtual int get_brand() const { return SPWPN_NORMAL; };
    virtual string get_name() const { return name; };
    virtual string get_verb() const { return get_name(); };
    int get_chance() const {
        const int base = get_base_chance();
        if (xl_based_chance())
            return base * (30 + you.experience_level) / 59;
        return base;
    }
    virtual int get_base_chance() const { return chance; }
    virtual bool xl_based_chance() const { return true; }
    virtual string describe() const;
protected:
    const int damage;
    // Per-attack trigger percent, before accounting for XL.
    const int chance;
    const string name;
};

class AuxConstrict: public AuxAttackType
{
public:
    AuxConstrict()
    : AuxAttackType(0, 100, "grab") { };
    bool xl_based_chance() const override { return false; }
};

class AuxKick: public AuxAttackType
{
public:
    AuxKick()
    : AuxAttackType(5, 100, "kick") { };

    int get_damage(bool /*random*/) const override
    {
        if (you.has_usable_hooves())
        {
            // Max hoof damage: 10.
            return damage + you.get_mutation_level(MUT_HOOVES) * 5 / 3;
        }

        if (you.has_usable_talons())
        {
            // Max talon damage: 9.
            return damage + 1 + you.get_mutation_level(MUT_TALONS);
        }

        // Max spike damage: 8.
        // ... yes, apparently tentacle spikes are "kicks".
        return damage + you.get_mutation_level(MUT_TENTACLE_SPIKE);
    }

    string get_verb() const override
    {
        if (you.has_usable_talons())
            return "claw";
        if (you.get_mutation_level(MUT_TENTACLE_SPIKE))
            return "pierce";
        return name;
    }

    string get_name() const override
    {
        if (you.get_mutation_level(MUT_TENTACLE_SPIKE))
            return "tentacle spike";
        return name;
    }
};

class AuxHeadbutt: public AuxAttackType
{
public:
    AuxHeadbutt()
    : AuxAttackType(5, 67, "headbutt") { };

    int get_damage(bool /*random*/) const override
    {
        return damage + you.get_mutation_level(MUT_HORNS) * 3;
    }
};

class AuxPeck: public AuxAttackType
{
public:
    AuxPeck()
    : AuxAttackType(6, 67, "peck") { };
};

class AuxTailslap: public AuxAttackType
{
public:
    AuxTailslap()
    : AuxAttackType(6, 50, "tail-slap") { };

    int get_damage(bool /*random*/) const override
    {
        return damage + max(0, you.get_mutation_level(MUT_STINGER) * 2 - 1)
                      + you.get_mutation_level(MUT_ARMOURED_TAIL) * 4
                      + you.get_mutation_level(MUT_WEAKNESS_STINGER);
    }

    int get_brand() const override
    {
        if (you.get_mutation_level(MUT_WEAKNESS_STINGER) == 3)
            return SPWPN_WEAKNESS;

        return you.get_mutation_level(MUT_STINGER) ? SPWPN_VENOM : SPWPN_NORMAL;
    }
};

class AuxPunch: public AuxAttackType
{
public:
    AuxPunch()
    : AuxAttackType(5, 0, "punch") { };

    int get_damage(bool random) const override
    {
        const int base_dam = damage + (random ? you.skill_rdiv(SK_UNARMED_COMBAT, 1, 2)
                                              : you.skill(SK_UNARMED_COMBAT) / 2);

        if (you.form == transformation::blade_hands)
            return base_dam + 6;

        if (you.has_usable_claws())
        {
            const int claws = you.has_claws();
            const int die_size = 3;
            // Don't use maybe_roll_dice because we want max, not mean.
            return base_dam + (random ? roll_dice(claws, die_size)
                                      : claws * die_size);
        }

        return base_dam;
    }

    string get_name() const override
    {
        if (you.form == transformation::blade_hands)
            return "slash";

        if (you.has_usable_claws())
            return "claw";

        if (you.has_usable_tentacles())
            return "tentacle-slap";

        return name;
    }

    int get_base_chance() const override
    {
        // Huh, this is a bit low. 5% at 0 UC, 50% at 27 UC..!
        // We don't div-rand-round because we want this to be
        // consistent for mut descriptions.
        return 5 + you.skill(SK_UNARMED_COMBAT, 5) / 3;
    }
};

class AuxBite: public AuxAttackType
{
public:
    AuxBite()
    : AuxAttackType(1, 40, "bite") { };

    int get_damage(bool random) const override
    {
        // duplicated in _describe_talisman_form
        const int fang_damage = damage + you.has_usable_fangs() * 2;

        if (you.get_mutation_level(MUT_ANTIMAGIC_BITE))
        {
            const int hd = you.get_hit_dice();
            const int denom = 3;
            return fang_damage + (random ? div_rand_round(hd, denom) : hd / denom);
        }

        if (you.get_mutation_level(MUT_ACIDIC_BITE))
            return fang_damage + (random ? roll_dice(2, 4) : 4);

        return fang_damage;
    }

    int get_brand() const override
    {
        if (you.get_mutation_level(MUT_ANTIMAGIC_BITE))
            return SPWPN_ANTIMAGIC;

        if (you.get_mutation_level(MUT_ACIDIC_BITE))
            return SPWPN_ACID;

        return SPWPN_NORMAL;
    }

    int get_base_chance() const override
    {
        if (you.get_mutation_level(MUT_ANTIMAGIC_BITE))
            return 100;
        return chance;
    }
};

class AuxPseudopods: public AuxAttackType
{
public:
    AuxPseudopods()
    : AuxAttackType(4, 67, "bludgeon") { };

    int get_damage(bool /*random*/) const override
    {
        return damage * you.has_usable_pseudopods();
    }
};

class AuxTentacles: public AuxAttackType
{
public:
    AuxTentacles()
    : AuxAttackType(12, 67, "squeeze") { };
};


class AuxTouch: public AuxAttackType
{
public:
    AuxTouch()
    : AuxAttackType(6, 40, "touch") { };

    int get_damage(bool random) const override
    {
        const int max = you.get_mutation_level(MUT_DEMONIC_TOUCH) * 4;
        return damage + (random ? random2(max + 1) : max);
    }

    int get_brand() const override
    {
        if (you.get_mutation_level(MUT_DEMONIC_TOUCH) == 3)
            return SPWPN_VULNERABILITY;

        return SPWPN_NORMAL;
    }

    bool xl_based_chance() const override { return false; }
};

class AuxMaw: public AuxAttackType
{
public:
    AuxMaw()
    : AuxAttackType(0, 75, "bite") { };
    int get_damage(bool random) const override {
        return get_form()->get_aux_damage(random);
    };
    bool xl_based_chance() const override { return false; }
};

static const AuxConstrict   AUX_CONSTRICT = AuxConstrict();
static const AuxKick        AUX_KICK = AuxKick();
static const AuxPeck        AUX_PECK = AuxPeck();
static const AuxHeadbutt    AUX_HEADBUTT = AuxHeadbutt();
static const AuxTailslap    AUX_TAILSLAP = AuxTailslap();
static const AuxTouch       AUX_TOUCH = AuxTouch();
static const AuxPunch       AUX_PUNCH = AuxPunch();
static const AuxBite        AUX_BITE = AuxBite();
static const AuxPseudopods  AUX_PSEUDOPODS = AuxPseudopods();
static const AuxTentacles   AUX_TENTACLES = AuxTentacles();
static const AuxMaw         AUX_MAW = AuxMaw();

static const AuxAttackType* const aux_attack_types[] =
{
    &AUX_CONSTRICT,
    &AUX_KICK,
    &AUX_HEADBUTT,
    &AUX_PECK,
    &AUX_TAILSLAP,
    &AUX_TOUCH,
    &AUX_PUNCH,
    &AUX_BITE,
    &AUX_PSEUDOPODS,
    &AUX_TENTACLES,
    &AUX_MAW,
};


/* Setup all unarmed (non attack_type) variables
 *
 * Clears any previous unarmed attack information and sets everything from
 * noise_factor to verb and damage. Called after player_aux_choose_uc_attack
 */
void melee_attack::player_aux_setup(unarmed_attack_type atk)
{
    const int num_aux_objs = ARRAYSZ(aux_attack_types);
    const int num_aux_atks = UNAT_LAST_ATTACK - UNAT_FIRST_ATTACK + 1;
    COMPILE_CHECK(num_aux_objs == num_aux_atks);

    ASSERT(atk >= UNAT_FIRST_ATTACK);
    ASSERT(atk <= UNAT_LAST_ATTACK);
    const AuxAttackType* const aux = aux_attack_types[atk - UNAT_FIRST_ATTACK];

    aux_damage = aux->get_damage(true);
    damage_brand = (brand_type)aux->get_brand();
    aux_attack = aux->get_name();
    aux_verb = aux->get_verb();

    if (wu_jian_attack != WU_JIAN_ATTACK_NONE)
        wu_jian_attack = WU_JIAN_ATTACK_TRIGGERED_AUX;

    if (atk == UNAT_BITE
        && _vamp_wants_blood_from_monster(defender->as_monster()))
    {
        damage_brand = SPWPN_VAMPIRISM;
    }
}

/**
 * Decide whether the player gets a bonus punch attack.
 *
 * @return  Whether the player gets a bonus punch aux attack on this attack.
 */
bool melee_attack::player_gets_aux_punch()
{
    if (!get_form()->can_offhand_punch())
        return false;

    // No punching with a shield or 2-handed wpn.
    // Octopodes aren't affected by this, though!
    if (you.arm_count() <= 2 && !you.has_usable_offhand())
        return false;

    return true;
}

bool melee_attack::player_aux_test_hit()
{
    // XXX We're clobbering did_hit
    did_hit = false;

    const int evasion = defender->evasion(false, attacker);

    if (player_under_penance(GOD_ELYVILON)
        && god_hates_your_god(GOD_ELYVILON)
        && to_hit >= evasion
        && one_chance_in(20))
    {
        simple_god_message(" blocks your attack.", GOD_ELYVILON);
        return false;
    }

    bool auto_hit = one_chance_in(30);

    if (to_hit >= evasion || auto_hit)
        return true;

    mprf("Your %s misses %s.", aux_attack.c_str(),
         defender->name(DESC_THE).c_str());

    return false;
}

/* Controls the looping on available unarmed attacks
 *
 * As the master method for unarmed player combat, this loops through
 * available unarmed attacks, determining whether they hit and - if so -
 * calculating and applying their damage.
 *
 * Returns (defender dead)
 */
bool melee_attack::player_aux_unarmed()
{
    unwind_var<brand_type> save_brand(damage_brand);

    for (int i = UNAT_FIRST_ATTACK; i <= UNAT_LAST_ATTACK; ++i)
    {
        if (!defender->alive())
            break;

        unarmed_attack_type atk = static_cast<unarmed_attack_type>(i);

        if (!_extra_aux_attack(atk))
            continue;

        // Determine and set damage and attack words.
        player_aux_setup(atk);

        if (atk == UNAT_CONSTRICT && !attacker->can_constrict(*defender, CONSTRICT_MELEE))
            continue;

        to_hit = random2(aux_to_hit());
        to_hit += post_roll_to_hit_modifiers(to_hit, false);

        handle_noise(defender->pos());
        alert_nearby_monsters();

        // [ds] kraken can flee when near death, causing the tentacle
        // the player was beating up to "die" and no longer be
        // available to answer questions beyond this point.
        // handle_noise stirs up all nearby monsters with a stick, so
        // the player may be beating up a tentacle, but the main body
        // of the kraken still gets a chance to act and submerge
        // tentacles before we get here.
        if (!defender->alive())
            return true;

        if (player_aux_test_hit())
        {
            // Upset the monster.
            behaviour_event(defender->as_monster(), ME_WHACK, attacker);
            if (!defender->alive())
                return true;

            if (attack_shield_blocked(true))
                continue;
            if (player_aux_apply(atk))
                return true;
        }
    }

    return false;
}

bool melee_attack::player_aux_apply(unarmed_attack_type atk)
{
    did_hit = true;

    count_action(CACT_MELEE, -1, atk); // aux_attack subtype/auxtype

    if (atk != UNAT_TOUCH)
    {
        aux_damage  = stat_modify_damage(aux_damage, SK_UNARMED_COMBAT, false);

        aux_damage  = random2(aux_damage);

        aux_damage  = apply_fighting_skill(aux_damage, true, true);

        aux_damage  = player_apply_misc_modifiers(aux_damage);

        aux_damage  = player_apply_slaying_bonuses(aux_damage, true);

        aux_damage  = player_apply_final_multipliers(aux_damage, true);

        if (atk == UNAT_CONSTRICT)
            aux_damage = 0;
        else
            aux_damage = apply_defender_ac(aux_damage);

        aux_damage = player_apply_postac_multipliers(aux_damage);
    }

    aux_damage = inflict_damage(aux_damage, BEAM_MISSILE);
    damage_done = aux_damage;

    if (defender->alive())
    {
        if (atk == UNAT_CONSTRICT)
            attacker->start_constricting(*defender);

        if (damage_done > 0 || atk == UNAT_CONSTRICT)
        {
            player_announce_aux_hit();

            if (damage_brand == SPWPN_ACID)
                defender->acid_corrode(3);

            if (damage_brand == SPWPN_VENOM && coinflip())
                poison_monster(defender->as_monster(), &you);

            if (damage_brand == SPWPN_WEAKNESS
                && !(defender->holiness() & (MH_UNDEAD | MH_NONLIVING)))
            {
                defender->weaken(&you, 6);
            }

            if (damage_brand == SPWPN_VULNERABILITY)
            {
                if (defender->strip_willpower(&you, random_range(4, 8), true))
                {
                    mprf("You sap %s willpower!",
                         defender->as_monster()->pronoun(PRONOUN_POSSESSIVE).c_str());
                }
            }

            // Normal vampiric biting attack, not if already got stabbing special.
            if (damage_brand == SPWPN_VAMPIRISM
                && you.has_mutation(MUT_VAMPIRISM)
                && (!stab_attempt || stab_bonus <= 0))
            {
                _player_vampire_draws_blood(defender->as_monster(), damage_done);
            }

            if (damage_brand == SPWPN_ANTIMAGIC && you.has_mutation(MUT_ANTIMAGIC_BITE)
                && damage_done > 0)
            {
                const bool spell_user = defender->antimagic_susceptible();

                antimagic_affects_defender(damage_done * 32);

                mprf("You drain %s %s.",
                     defender->as_monster()->pronoun(PRONOUN_POSSESSIVE).c_str(),
                     spell_user ? "magic" : "power");

                if (you.magic_points != you.max_magic_points
                    && !defender->as_monster()->is_summoned()
                    && !mons_is_firewood(*defender->as_monster()))
                {
                    int drain = random2(damage_done * 2) + 1;
                    // Augment mana drain--1.25 "standard" effectiveness at 0 mp,
                    // 0.25 at mana == max_mana
                    drain = (int)((1.25 - you.magic_points / you.max_magic_points)
                                  * drain);
                    if (drain)
                    {
                        mpr("You feel invigorated.");
                        inc_mp(drain);
                    }
                }
            }
        }
        else // no damage was done
        {
            mprf("You %s %s%s.",
                 aux_verb.c_str(),
                 defender->name(DESC_THE).c_str(),
                 you.can_see(*defender) ? ", but do no damage" : "");
        }
    }
    else // defender was just alive, so this call should be ok?
        player_announce_aux_hit();

    if (defender->as_monster()->hit_points < 1)
    {
        handle_phase_killed();
        return true;
    }

    return false;
}

void melee_attack::player_announce_aux_hit()
{
    mprf("You %s %s%s%s",
         aux_verb.c_str(),
         defender->name(DESC_THE).c_str(),
         debug_damage_number().c_str(),
         attack_strength_punctuation(damage_done).c_str());
}

void melee_attack::player_warn_miss()
{
    did_hit = false;

    mprf("You%s miss %s.",
         evasion_margin_adverb().c_str(),
         defender->name(DESC_THE).c_str());
}

// A couple additive modifiers that should be applied to both unarmed and
// armed attacks.
int melee_attack::player_apply_misc_modifiers(int damage)
{
    if (you.duration[DUR_MIGHT] || you.duration[DUR_BERSERK])
        damage += 1 + random2(10);

    return damage;
}

// Multipliers to be applied to the final (pre-stab, pre-AC) damage.
// It might be tempting to try to pick and choose what pieces of the damage
// get affected by such multipliers, but putting them at the end is the
// simplest effect to understand if they aren't just going to be applied
// to the base damage of the weapon.
int melee_attack::player_apply_final_multipliers(int damage, bool aux)
{
    // cleave damage modifier
    if (cleaving)
        damage = cleave_damage_mod(damage);

    // martial damage modifier (wu jian)
    damage = martial_damage_mod(damage);

    // Electric charge bonus.
    if (charge_pow > 0 && defender->res_elec() <= 0)
        damage += div_rand_round(damage * charge_pow, 150);

    // Can't affect much of anything as a shadow.
    if (you.form == transformation::shadow)
        damage = div_rand_round(damage, 2);

    if (you.duration[DUR_WEAK])
        damage = div_rand_round(damage * 3, 4);

    if (you.duration[DUR_CONFUSING_TOUCH] && !aux)
        return 0;

    return damage;
}

int melee_attack::player_apply_postac_multipliers(int damage)
{
    // Statue form's damage modifier is designed to exactly compensate for
    // the slowed speed; therefore, it needs to apply after AC.
    // Flux form's modifier is the inverse of statue form's.
    if (you.form == transformation::statue)
        damage = div_rand_round(damage * 3, 2);
    else if (you.form == transformation::flux)
        damage = div_rand_round(damage * 2, 3);

    return damage;
}

void melee_attack::set_attack_verb(int damage)
{
    if (!attacker->is_player())
        return;

    int weap_type = WPN_UNKNOWN;

    if (Options.has_fake_lang(flang_t::grunt))
        damage = HIT_STRONG + 1;

    if (!weapon)
        weap_type = WPN_UNARMED;
    else if (weapon->base_type == OBJ_STAVES)
        weap_type = WPN_STAFF;
    else if (weapon->base_type == OBJ_WEAPONS
             && !is_range_weapon(*weapon))
    {
        weap_type = weapon->sub_type;
    }

    // All weak hits with weapons look the same.
    if (damage < HIT_WEAK
        && weap_type != WPN_UNARMED)
    {
        if (weap_type != WPN_UNKNOWN)
            attack_verb = "hit";
        else
            attack_verb = "clumsily bash";
        return;
    }

    // Take normal hits into account. If the hit is from a weapon with
    // more than one damage type, randomly choose one damage type from
    // it.
    monster_type defender_genus = mons_genus(defender->type);
    switch (weapon ? single_damage_type(*weapon) : -1)
    {
    case DAM_PIERCE:
        if (damage < HIT_MED)
            attack_verb = "puncture";
        else if (damage < HIT_STRONG)
            attack_verb = "impale";
        else
        {
            if (defender->is_monster()
                && defender_visible
                && defender_genus == MONS_HOG)
            {
                attack_verb = "spit";
                verb_degree = "like the proverbial pig";
            }
            else if (defender_genus == MONS_CRAB
                     && Options.has_fake_lang(flang_t::grunt))
            {
                attack_verb = "attack";
                verb_degree = "'s weak point";
            }
            else
            {
                static const char * const pierce_desc[][2] =
                {
                    {"spit", "like a pig"},
                    {"skewer", "like a kebab"},
                    {"stick", "like a pincushion"},
                    {"perforate", "like a sieve"}
                };
                const int choice = random2(ARRAYSZ(pierce_desc));
                attack_verb = pierce_desc[choice][0];
                verb_degree = pierce_desc[choice][1];
            }
        }
        break;

    case DAM_SLICE:
        if (damage < HIT_MED)
            attack_verb = "slash";
        else if (damage < HIT_STRONG)
            attack_verb = "slice";
        else if (defender_genus == MONS_OGRE)
        {
            attack_verb = "dice";
            verb_degree = "like an onion";
        }
        else if (defender_genus == MONS_SKELETON)
        {
            attack_verb = "fracture";
            verb_degree = "into splinters";
        }
        else if (defender_genus == MONS_HOG)
        {
            attack_verb = "carve";
            verb_degree = "like the proverbial ham";
        }
        else if ((defender_genus == MONS_TENGU
                  || get_mon_shape(defender_genus) == MON_SHAPE_BIRD)
                 && one_chance_in(3))
        {
            attack_verb = "carve";
            verb_degree = "like a turkey";
        }
        else if ((defender_genus == MONS_YAK || defender_genus == MONS_YAKTAUR)
                 && Options.has_fake_lang(flang_t::grunt))
        {
            attack_verb = "shave";
        }
        else
        {
            static const char * const slice_desc[][2] =
            {
                {"open",    "like a pillowcase"},
                {"slice",   "like a ripe choko"},
                {"cut",     "into ribbons"},
                {"carve",   "like a ham"},
                {"chop",    "into pieces"}
            };
            const int choice = random2(ARRAYSZ(slice_desc));
            attack_verb = slice_desc[choice][0];
            verb_degree = slice_desc[choice][1];
        }
        break;

    case DAM_BLUDGEON:
        if (damage < HIT_MED)
            attack_verb = one_chance_in(4) ? "thump" : "sock";
        else if (damage < HIT_STRONG)
            attack_verb = "bludgeon";
        else if (defender_genus == MONS_SKELETON)
        {
            attack_verb = "shatter";
            verb_degree = "into splinters";
        }
        else if (defender->type == MONS_GREAT_ORB_OF_EYES)
        {
            attack_verb = "splatter";
            verb_degree = "into a gooey mess";
        }
        else
        {
            static const char * const bludgeon_desc[][2] =
            {
                {"crush",   "like a grape"},
                {"beat",    "like a drum"},
                {"hammer",  "like a gong"},
                {"pound",   "like an anvil"},
                {"flatten", "like a pancake"}
            };
            const int choice = random2(ARRAYSZ(bludgeon_desc));
            attack_verb = bludgeon_desc[choice][0];
            verb_degree = bludgeon_desc[choice][1];
        }
        break;

    case DAM_WHIP:
        if (damage < HIT_MED)
            attack_verb = "whack";
        else if (damage < HIT_STRONG)
            attack_verb = "thrash";
        else
        {
            if (defender->holiness() & (MH_HOLY | MH_NATURAL | MH_DEMONIC))
            {
                attack_verb = "punish";
                verb_degree = ", causing immense pain";
                break;
            }
            else
                attack_verb = "devastate";
        }
        break;

    case -1: // unarmed
    {
        const FormAttackVerbs verbs = get_form(you.form)->uc_attack_verbs;
        if (verbs.weak != nullptr)
        {
            if (damage < HIT_WEAK)
                attack_verb = verbs.weak;
            else if (damage < HIT_MED)
                attack_verb = verbs.medium;
            else if (damage < HIT_STRONG)
                attack_verb = verbs.strong;
            else
                attack_verb = verbs.devastating;
            break;
        }

        if (you.damage_type() == DVORP_CLAWING)
        {
            if (damage < HIT_WEAK)
                attack_verb = "scratch";
            else if (damage < HIT_MED)
                attack_verb = "claw";
            else if (damage < HIT_STRONG)
                attack_verb = "mangle";
            else
                attack_verb = "eviscerate";
        }
        else if (you.damage_type() == DVORP_TENTACLE)
        {
            if (damage < HIT_WEAK)
                attack_verb = "tentacle-slap";
            else if (damage < HIT_MED)
                attack_verb = "bludgeon";
            else if (damage < HIT_STRONG)
                attack_verb = "batter";
            else
                attack_verb = "thrash";
        }
        else
        {
            if (damage < HIT_WEAK)
                attack_verb = "hit";
            else if (damage < HIT_MED)
                attack_verb = "punch";
            else if (damage < HIT_STRONG)
                attack_verb = "pummel";
            else if (defender->is_monster()
                     && mons_genus(defender->type) == MONS_FORMICID)
            {
                attack_verb = "squash";
                verb_degree = "like the proverbial ant";
            }
            else
            {
                static const char * const punch_desc[][2] =
                {
                    {"pound",     "into fine dust"},
                    {"pummel",    "like a punching bag"},
                    {"pulverise", ""},
                    {"squash",    "like an ant"}
                };
                const int choice = random2(ARRAYSZ(punch_desc));
                // XXX: could this distinction work better?
                if (choice == 0
                    && defender->is_monster()
                    && mons_has_blood(defender->type))
                {
                    attack_verb = "beat";
                    verb_degree = "into a bloody pulp";
                }
                else
                {
                    attack_verb = punch_desc[choice][0];
                    verb_degree = punch_desc[choice][1];
                }
            }
        }
        break;
    }

    case WPN_UNKNOWN:
    default:
        attack_verb = "hit";
        break;
    }
}

void melee_attack::player_exercise_combat_skills()
{
    if (defender && defender->is_monster()
        && !mons_is_firewood(*defender->as_monster()))
    {
        practise_hitting(weapon);
    }
}

/*
 * Applies god conduct for weapon ego
 *
 * Using speed brand as a chei worshipper, or holy/unholy/wizardly weapons etc
 */
void melee_attack::player_weapon_upsets_god()
{
    if (weapon
        && (weapon->base_type == OBJ_WEAPONS || weapon->base_type == OBJ_STAVES)
        && god_hates_item_handling(*weapon))
    {
        did_god_conduct(god_hates_item_handling(*weapon), 2);
    }
}

/* Apply player-specific effects as well as brand damage.
 *
 * Called after damage is calculated, but before unrand effects and before
 * damage is dealt.
 *
 * Returns true if combat should continue, false if it should end here.
 */
bool melee_attack::player_monattk_hit_effects()
{
    player_weapon_upsets_god();

    // Don't even check vampire bloodletting if the monster has already
    // been reset (for example, a spectral weapon who noticed in
    // player_stab_check that it shouldn't exist anymore).
    if (defender->type == MONS_NO_MONSTER)
        return false;

    // Thirsty vampires will try to use a stabbing situation to draw blood.
    if (you.has_mutation(MUT_VAMPIRISM)
        && damage_done > 0
        && stab_attempt
        && stab_bonus > 0)
    {
        _player_vampire_draws_blood(defender->as_monster(), damage_done, true);
    }

    if (!defender->alive())
        return false;

    // These effects apply only to monsters that are still alive:

    // Returns true if the hydra was killed by the decapitation, in which case
    // nothing more should be done to the hydra.
    if (consider_decapitation(damage_done))
        return false;

    return true;
}

/**
 * If appropriate, chop a head off the defender. (Usually a hydra.)
 *
 * @param dam           The damage done in the attack that may or may not chop
  *                     off a head.
 * @param damage_type   The type of damage done in the attack.
 * @return              Whether the defender was killed by the decapitation.
 */
bool melee_attack::consider_decapitation(int dam, int damage_type)
{
    const int dam_type = (damage_type != -1) ? damage_type :
                                               attacker->damage_type();
    if (!attack_chops_heads(dam, dam_type))
        return false;

    decapitate(dam_type);

    if (!defender->alive())
        return true;

    // Only living hydras get to regenerate heads.
    if (!(defender->holiness() & MH_NATURAL))
        return false;

    // What's the largest number of heads the defender can have?
    const int limit = defender->type == MONS_LERNAEAN_HYDRA ? 27 : 20;

    if (attacker->damage_brand() == SPWPN_FLAMING)
    {
        if (defender_visible)
            mpr("The flame cauterises the wound!");
        return false;
    }

    int heads = defender->heads();
    if (heads >= limit - 1)
        return false; // don't overshoot the head limit!

    simple_monster_message(*defender->as_monster(), " grows two more!");
    defender->as_monster()->num_heads += 2;
    defender->heal(8 + random2(8));

    return false;
}

/**
 * Can the given actor lose its heads? (Is it hydra or hydra-like?)
 *
 * @param defender  The actor in question.
 * @return          Whether the given actor is susceptible to head-choppage.
 */
static bool actor_can_lose_heads(const actor* defender)
{
    if (defender->is_monster()
        && defender->as_monster()->has_hydra_multi_attack()
        && defender->as_monster()->mons_species() != MONS_SPECTRAL_THING
        && defender->as_monster()->mons_species() != MONS_SERPENT_OF_HELL)
    {
        return true;
    }

    return false;
}

/**
 * Does this attack chop off one of the defender's heads? (Generally only
 * relevant for hydra defenders)
 *
 * @param dam           The damage done in the attack in question.
 * @param dam_type      The vorpal_damage_type of the attack.
 * @param wpn_brand     The brand_type of the attack.
 * @return              Whether the attack will chop off a head.
 */
bool melee_attack::attack_chops_heads(int dam, int dam_type)
{
    // hydras and hydra-like things only.
    if (!actor_can_lose_heads(defender))
        return false;

    // no decapitate on riposte (Problematic)
    if (is_riposte)
        return false;

    // Monster attackers+defenders have only a 25% chance of making the
    // chop-check to prevent runaway head inflation.
    // XXX: Tentatively making an exception for spectral weapons
    const bool player_spec_weap = attacker->is_monster()
                                    && attacker->type == MONS_SPECTRAL_WEAPON
                                    && attacker->as_monster()->summoner
                                        == MID_PLAYER;
    if (attacker->is_monster() && defender->is_monster()
        && !player_spec_weap && !one_chance_in(4))
    {
        return false;
    }

    // Only cutting implements.
    if (dam_type != DVORP_SLICING && dam_type != DVORP_CHOPPING
        && dam_type != DVORP_CLAWING)
    {
        return false;
    }

    // Small claws are not big enough.
    if (dam_type == DVORP_CLAWING && attacker->has_claws() < 3)
        return false;

    // You need to have done at least some damage.
    if (dam <= 0 || dam < 4 && coinflip())
        return false;

    // ok, good enough!
    return true;
}

/**
 * Decapitate the (hydra or hydra-like) defender!
 *
 * @param dam_type      The vorpal_damage_type of the attack.
 */
void melee_attack::decapitate(int dam_type)
{
    // Player hydras don't gain or lose heads.
    ASSERT(defender->is_monster());

    const char *verb = nullptr;

    if (dam_type == DVORP_CLAWING)
    {
        static const char *claw_verbs[] = { "rip", "tear", "claw" };
        verb = RANDOM_ELEMENT(claw_verbs);
    }
    else
    {
        static const char *slice_verbs[] =
        {
            "slice", "lop", "chop", "hack"
        };
        verb = RANDOM_ELEMENT(slice_verbs);
    }

    int heads = defender->heads();
    if (heads == 1) // will be zero afterwards
    {
        if (defender_visible)
        {
            mprf("%s %s %s last head off!",
                 atk_name(DESC_THE).c_str(),
                 attacker->conj_verb(verb).c_str(),
                 apostrophise(defender_name(true)).c_str());
        }

        if (!defender->is_summoned())
        {
            bleed_onto_floor(defender->pos(), defender->type,
                             defender->as_monster()->hit_points, true);
        }

        if (!simu)
            defender->hurt(attacker, INSTANT_DEATH);

        return;
    }

    if (defender_visible)
    {
        mprf("%s %s one of %s heads off!",
             atk_name(DESC_THE).c_str(),
             attacker->conj_verb(verb).c_str(),
             apostrophise(defender_name(true)).c_str());
    }

    defender->as_monster()->num_heads--;
}

/**
 * Apply passive retaliation damage from hitting acid monsters.
 */
void melee_attack::attacker_sustain_passive_damage()
{
    // If the defender has been cleaned up, it's too late for anything.
    if (!defender->alive())
        return;

    if (!mons_class_flag(defender->type, M_ACID_SPLASH))
        return;

    if (attacker->res_acid() >= 3)
        return;

    if (!adjacent(attacker->pos(), defender->pos()) || is_riposte)
        return;

    const int acid_strength = resist_adjust_damage(attacker, BEAM_ACID, 5);

    // Spectral weapons can't be corroded (but can take acid damage).
    const bool avatar = attacker->is_monster()
                        && mons_is_avatar(attacker->as_monster()->type);

    if (!avatar)
    {
        if (x_chance_in_y(acid_strength + 1, 30))
            attacker->corrode_equipment();
    }

    if (attacker->is_player())
        mpr(you.hands_act("burn", "!"));
    else
    {
        simple_monster_message(*attacker->as_monster(),
                               " is burned by acid!");
    }
    attacker->hurt(defender, roll_dice(1, acid_strength), BEAM_ACID,
                   KILLED_BY_ACID);
}

int melee_attack::staff_damage(skill_type skill)
{
    if (x_chance_in_y(attacker->skill(SK_EVOCATIONS, 200)
                    + attacker->skill(skill, 100), 3000))
    {
        return random2((attacker->skill(skill, 100)
                      + attacker->skill(SK_EVOCATIONS, 50)) / 80);
    }
    return 0;
}

bool melee_attack::apply_staff_damage()
{
    if (!weapon)
        return false;

    if (attacker->is_player() && you.get_mutation_level(MUT_NO_ARTIFICE))
        return false;

    if (weapon->base_type != OBJ_STAVES)
        return false;

    skill_type sk = staff_skill(static_cast<stave_type>(weapon->sub_type));

    switch (weapon->sub_type)
    {
    case STAFF_AIR:
        special_damage =
            resist_adjust_damage(defender, BEAM_ELECTRICITY, staff_damage(sk));

        if (special_damage)
        {
            special_damage_message =
                make_stringf(
                    "%s %s electrocuted%s",
                    defender->name(DESC_THE).c_str(),
                    defender->conj_verb("are").c_str(),
                    attack_strength_punctuation(special_damage).c_str());
            special_damage_flavour = BEAM_ELECTRICITY;
        }

        break;

    case STAFF_COLD:
        special_damage =
            resist_adjust_damage(defender, BEAM_COLD, staff_damage(sk));

        if (special_damage)
        {
            special_damage_message =
                make_stringf(
                    "%s freeze%s %s%s",
                    attacker->name(DESC_THE).c_str(),
                    attacker->is_player() ? "" : "s",
                    defender->name(DESC_THE).c_str(),
                    attack_strength_punctuation(special_damage).c_str());
            special_damage_flavour = BEAM_COLD;
        }
        break;

    case STAFF_EARTH:
        special_damage = staff_damage(sk) * 5 / 4;
        special_damage = apply_defender_ac(special_damage, 0);
        if (defender->airborne())
            special_damage /= 3;

        if (special_damage > 0)
        {
            special_damage_message =
                make_stringf(
                    "The ground beneath %s fractures%s",
                    defender->name(DESC_THE).c_str(),
                    attack_strength_punctuation(special_damage).c_str());
        }
        break;

    case STAFF_FIRE:
        special_damage =
            resist_adjust_damage(defender, BEAM_FIRE, staff_damage(sk));

        if (special_damage)
        {
            special_damage_message =
                make_stringf(
                    "%s burn%s %s%s",
                    attacker->name(DESC_THE).c_str(),
                    attacker->is_player() ? "" : "s",
                    defender->name(DESC_THE).c_str(),
                    attack_strength_punctuation(special_damage).c_str());
            special_damage_flavour = BEAM_FIRE;

            if (defender->is_player())
                maybe_melt_player_enchantments(BEAM_FIRE, special_damage);
        }
        break;

    case STAFF_ALCHEMY:
        special_damage =
            resist_adjust_damage(defender, BEAM_POISON, staff_damage(sk));

        if (special_damage)
        {
            special_damage_message =
                make_stringf(
                    "%s envenom%s %s%s",
                    attacker->name(DESC_THE).c_str(),
                    attacker->is_player() ? "" : "s",
                    defender->name(DESC_THE).c_str(),
                    attack_strength_punctuation(special_damage).c_str());
            special_damage_flavour = BEAM_POISON;
        }
        break;

    case STAFF_DEATH:
        special_damage =
            resist_adjust_damage(defender, BEAM_NEG, staff_damage(sk));

        if (special_damage)
        {
            special_damage_message =
                make_stringf(
                    "%s %s in agony%s",
                    defender->name(DESC_THE).c_str(),
                    defender->conj_verb("writhe").c_str(),
                    attack_strength_punctuation(special_damage).c_str());

            attacker->god_conduct(DID_EVIL, 4);
        }
        break;

    case STAFF_CONJURATION:
        special_damage = staff_damage(sk);
        special_damage = apply_defender_ac(special_damage);

        if (special_damage > 0)
        {
            special_damage_message =
                make_stringf(
                    "%s %s %s%s",
                    attacker->name(DESC_THE).c_str(),
                    attacker->conj_verb("blast").c_str(),
                    defender->name(DESC_THE).c_str(),
                    attack_strength_punctuation(special_damage).c_str());
        }
        break;

#if TAG_MAJOR_VERSION == 34
    case STAFF_SUMMONING:
    case STAFF_POWER:
    case STAFF_ENCHANTMENT:
    case STAFF_ENERGY:
    case STAFF_WIZARDRY:
#endif
        break;

    default:
        die("Invalid staff type: %d", weapon->sub_type);
    }

    if (special_damage || special_damage_flavour)
    {
        dprf(DIAG_COMBAT, "Staff damage to %s: %d, flavour: %d",
             defender->name(DESC_THE).c_str(),
             special_damage, special_damage_flavour);

        if (needs_message && !special_damage_message.empty())
            mpr(special_damage_message);

        inflict_damage(special_damage, special_damage_flavour);
        if (special_damage > 0)
        {
            defender->expose_to_element(special_damage_flavour, 2);
            // XXX: this is messy, but poisoning from the staff of poison
            // should happen after damage.
            if (defender->alive() && special_damage_flavour == BEAM_POISON)
                defender->poison(attacker, 2);
        }
    }

    return true;
}

int melee_attack::calc_to_hit(bool random)
{
    int mhit = attack::calc_to_hit(random);
    if (mhit == AUTOMATIC_HIT)
        return AUTOMATIC_HIT;

    return mhit;
}

int melee_attack::post_roll_to_hit_modifiers(int mhit, bool random)
{
    int modifiers = attack::post_roll_to_hit_modifiers(mhit, random);

    // Electric charges feel bad when they miss, so make them miss less often.
    if (charge_pow > 0)
        modifiers += 5;

    return modifiers;
}

void melee_attack::player_stab_check()
{
    if (!is_projected)
        attack::player_stab_check();
}

/**
 * Can we get a good stab with this weapon?
 */
bool melee_attack::player_good_stab()
{
    return wpn_skill == SK_SHORT_BLADES
           || you.get_mutation_level(MUT_PAWS)
           || player_equip_unrand(UNRAND_HOOD_ASSASSIN)
              && (!weapon || is_melee_weapon(*weapon));
}

/* Select the attack verb for attacker
 *
 * If klown, select randomly from klown_attack, otherwise check for any special
 * case attack verbs (tentacles or door/fountain-mimics) and if all else fails,
 * select an attack verb from attack_types based on the ENUM value of attk_type.
 *
 * Returns (attack_verb)
 */
string melee_attack::mons_attack_verb()
{
    static const char *klown_attack[] =
    {
        "hit",
        "poke",
        "prod",
        "flog",
        "pound",
        "slap",
        "tickle",
        "defenestrate",
        "sucker-punch",
        "elbow",
        "pinch",
        "strangle-hug",
        "squeeze",
        "tease",
        "eye-gouge",
        "karate-kick",
        "headlock",
        "wrestle",
        "trip-wire",
        "kneecap"
    };

    if (attacker->type == MONS_KILLER_KLOWN && attk_type == AT_HIT)
        return RANDOM_ELEMENT(klown_attack);

    //XXX: then why give them it in the first place?
    if (attk_type == AT_TENTACLE_SLAP && mons_is_tentacle(attacker->type))
        return "slap";

    return mon_attack_name(attk_type);
}

string melee_attack::mons_attack_desc()
{
    if (!you.can_see(*attacker))
        return "";

    string ret;
    int dist = (attack_position - defender->pos()).rdist();
    if (dist > 1)
    {
        ASSERT(can_reach());
        ret = " from afar";
    }

    if (weapon && !mons_class_is_animated_weapon(attacker->type))
        ret += " with " + weapon->name(DESC_A);

    return ret;
}

string melee_attack::charge_desc()
{
    if (!charge_pow || defender->res_elec() > 0)
        return "";
    const string pronoun = defender->pronoun(PRONOUN_OBJECTIVE);
    return make_stringf(" and electrocute %s", pronoun.c_str());
}

void melee_attack::announce_hit()
{
    if (!needs_message || attk_flavour == AF_CRUSH)
        return;

    if (attacker->is_monster())
    {
        mprf("%s %s %s%s%s%s",
             atk_name(DESC_THE).c_str(),
             attacker->conj_verb(mons_attack_verb()).c_str(),
             defender_name(true).c_str(),
             debug_damage_number().c_str(),
             mons_attack_desc().c_str(),
             attack_strength_punctuation(damage_done).c_str());
    }
    else
    {
        if (!verb_degree.empty() && verb_degree[0] != ' '
            && verb_degree[0] != ',' && verb_degree[0] != '\'')
        {
            verb_degree = " " + verb_degree;
        }

        mprf("You %s %s%s%s%s%s",
             attack_verb.c_str(),
             defender->name(DESC_THE).c_str(), verb_degree.c_str(),
             charge_desc().c_str(), debug_damage_number().c_str(),
             attack_strength_punctuation(damage_done).c_str());
    }
}

// Returns if the target was actually poisoned by this attack
bool melee_attack::mons_do_poison()
{
    int amount;
    const int hd = attacker->get_hit_dice();

    if (attk_flavour == AF_POISON_STRONG)
        amount = random_range(hd * 11 / 3, hd * 13 / 2);
    else if (attk_flavour == AF_MINIPARA)
        amount = random_range(hd, hd * 2);
    else
        amount = random_range(hd * 2, hd * 4);

    if (attacker->as_monster()->has_ench(ENCH_CONCENTRATE_VENOM))
    {
        // Attach our base poison damage to the curare effect
        return curare_actor(attacker, defender, "concentrated venom",
                            attacker->name(DESC_PLAIN), amount);
    }

    if (!defender->poison(attacker, amount))
        return false;

    if (needs_message)
    {
        mprf("%s poisons %s!",
                atk_name(DESC_THE).c_str(),
                defender_name(true).c_str());
    }

    return true;
}

static void _print_resist_messages(actor* defender, int base_damage,
                                   beam_type flavour)
{
    // check_your_resists is used for the player case to get additional
    // effects such as Xom amusement, melting of icy effects, etc.
    // mons_adjust_flavoured is used for the monster case to get all of the
    // special message handling ("The ice beast melts!") correct.
    // XXX: there must be a nicer way to do this, especially because we're
    // basically calculating the damage twice in the case where messages
    // are needed.
    if (defender->is_player())
        (void)check_your_resists(base_damage, flavour, "");
    else
    {
        bolt beam;
        beam.flavour = flavour;
        (void)mons_adjust_flavoured(defender->as_monster(),
                                    beam,
                                    base_damage,
                                    true);
    }
}

bool melee_attack::mons_attack_effects()
{
    // may have died earlier, due to e.g. pain bond
    // we could continue with the rest of their attack, but it's a minefield
    // of potential crashes. so, let's not.
    if (attacker->is_monster() && invalid_monster(attacker->as_monster()))
        return false;

    // Monsters attacking themselves don't get attack flavour.
    // The message sequences look too weird. Also, stealing
    // attacks aren't handled until after the damage msg. Also,
    // no attack flavours for dead defenders
    if (attacker != defender && defender->alive())
    {
        mons_apply_attack_flavour();

        if (needs_message && !special_damage_message.empty())
            mpr(special_damage_message);

        if (special_damage > 0)
        {
            inflict_damage(special_damage, special_damage_flavour);
            special_damage = 0;
            special_damage_message.clear();
            special_damage_flavour = BEAM_NONE;
        }
    }

    if (defender->is_player())
        practise_being_hit();

    // A tentacle may have banished its own parent/sibling and thus itself.
    if (!attacker->alive())
        return false;

    // consider_decapitation() returns true if the defender was killed
    // by the decapitation, in which case we should stop the rest of the
    // attack, too.
    if (consider_decapitation(damage_done,
                              attacker->damage_type(attack_number)))
    {
        return false;
    }

    const bool slippery = defender->is_player()
                          && adjacent(attacker->pos(), defender->pos())
                          && !player_stair_delay() // feet otherwise occupied
                          && player_equip_unrand(UNRAND_SLICK_SLIPPERS);
    if (attacker != defender && (attk_flavour == AF_TRAMPLE ||
                                 slippery && attk_flavour != AF_DRAG))
    {
        do_knockback(slippery);
    }

    if (attacker != defender && attk_flavour == AF_DRAG)
        do_drag();

    special_damage = 0;
    special_damage_message.clear();
    special_damage_flavour = BEAM_NONE;

    // Defender banished. Bail since the defender is still alive in the
    // Abyss.
    if (defender->is_banished())
        return false;

    if (!defender->alive())
        return attacker->alive();

    // Bail if the monster is attacking itself without a weapon, since
    // intrinsic monster attack flavours aren't applied for self-attacks.
    if (attacker == defender && !weapon)
        return false;

    return true;
}

void melee_attack::mons_apply_attack_flavour()
{
    // Most of this is from BWR 4.1.2.

    attack_flavour flavour = attk_flavour;
    if (flavour == AF_CHAOTIC)
        flavour = random_chaos_attack_flavour();

    const int base_damage = flavour_damage(flavour, attacker->get_hit_dice());

    // Note that if damage_done == 0 then this code won't be reached
    // unless the flavour is in flavour_triggers_damageless.
    switch (flavour)
    {
    default:
        // Just to trigger special melee damage effects for regular attacks
        // (e.g. Qazlal's elemental adaptation).
        defender->expose_to_element(BEAM_MISSILE, 2);
        break;

    case AF_POISON:
    case AF_POISON_STRONG:
    case AF_REACH_STING:
        if (attacker->as_monster()->has_ench(ENCH_CONCENTRATE_VENOM)
            ? coinflip()
            : one_chance_in(3))
        {
            mons_do_poison();
        }
        break;

    case AF_FIRE:
        special_damage =
            resist_adjust_damage(defender,
                                 BEAM_FIRE,
                                 base_damage);
        special_damage_flavour = BEAM_FIRE;

        if (needs_message && base_damage)
        {
            mprf("%s %s engulfed in flames%s",
                 defender_name(false).c_str(),
                 defender->conj_verb("are").c_str(),
                 attack_strength_punctuation(special_damage).c_str());

            _print_resist_messages(defender, base_damage, BEAM_FIRE);
        }

        defender->expose_to_element(BEAM_FIRE, 2);
        break;

    case AF_COLD:
        special_damage =
            resist_adjust_damage(defender,
                                 BEAM_COLD,
                                 base_damage);
        special_damage_flavour = BEAM_COLD;

        if (needs_message && base_damage)
        {
            mprf("%s %s %s%s",
                 atk_name(DESC_THE).c_str(),
                 attacker->conj_verb("freeze").c_str(),
                 defender_name(true).c_str(),
                 attack_strength_punctuation(special_damage).c_str());

            _print_resist_messages(defender, base_damage, BEAM_COLD);
        }

        defender->expose_to_element(BEAM_COLD, 2);
        break;

    case AF_ELEC:
        special_damage =
            resist_adjust_damage(defender,
                                 BEAM_ELECTRICITY,
                                 base_damage);
        special_damage_flavour = BEAM_ELECTRICITY;

        if (needs_message && base_damage)
        {
            mprf("%s %s %s%s",
                 atk_name(DESC_THE).c_str(),
                 attacker->conj_verb("shock").c_str(),
                 defender_name(true).c_str(),
                 attack_strength_punctuation(special_damage).c_str());

            _print_resist_messages(defender, base_damage, BEAM_ELECTRICITY);
        }

        dprf(DIAG_COMBAT, "Shock damage: %d", special_damage);
        defender->expose_to_element(BEAM_ELECTRICITY, 2);
        break;

        // Combines drain speed and vampiric.
    case AF_SCARAB:
        if (x_chance_in_y(3, 5))
            drain_defender_speed();

        // deliberate fall-through
    case AF_VAMPIRIC:
        if (!actor_is_susceptible_to_vampirism(*defender))
            break;

        if (defender->stat_hp() < defender->stat_maxhp())
        {
            int healed = resist_adjust_damage(defender, BEAM_NEG,
                                              1 + random2(damage_done));
            if (healed)
            {
                attacker->heal(healed);
                if (needs_message)
                {
                    mprf("%s %s strength from %s injuries!",
                         atk_name(DESC_THE).c_str(),
                         attacker->conj_verb("draw").c_str(),
                         def_name(DESC_ITS).c_str());
                }
            }
        }
        break;

    case AF_DRAIN_STR:
    case AF_DRAIN_INT:
    case AF_DRAIN_DEX:
        if (one_chance_in(20) || one_chance_in(3))
        {
            stat_type drained_stat = (flavour == AF_DRAIN_STR ? STAT_STR :
                                      flavour == AF_DRAIN_INT ? STAT_INT
                                                              : STAT_DEX);
            defender->drain_stat(drained_stat, 1);
        }
        break;

    case AF_BLINK:
        // blinking can kill, delay the call
        if (one_chance_in(3))
            blink_fineff::schedule(attacker);
        break;

    case AF_BLINK_WITH:
        if (coinflip())
            blink_fineff::schedule(attacker, defender);
        break;

    case AF_CONFUSE:
        if (attk_type == AT_SPORE)
        {
            if (defender->is_unbreathing())
                break;

            monster *attkmon = attacker->as_monster();
            attkmon->set_hit_dice(attkmon->get_experience_level() - 1);
            if (attkmon->get_experience_level() <= 0)
                attacker->as_monster()->suicide();

            if (defender_visible)
            {
                mprf("%s %s engulfed in a cloud of dizzying spores!",
                     defender->name(DESC_THE).c_str(),
                     defender->conj_verb("are").c_str());
            }
        }

        if (one_chance_in(3))
        {
            if (attk_type != AT_SPORE)
            {
                mprf("%s %s afflicted by dizzying energies!",
                     defender->name(DESC_THE).c_str(),
                     defender->conj_verb("are").c_str());
            }
            defender->confuse(attacker,
                              1 + random2(3+attacker->get_hit_dice()));
        }
        break;

    case AF_DRAIN:
        if (coinflip())
            drain_defender();
        break;

    case AF_BARBS:
        if (defender->is_player())
        {
            mpr("Barbed spikes become lodged in your body.");
            // same duration as manticore barbs
            if (!you.duration[DUR_BARBS])
                you.set_duration(DUR_BARBS, random_range(4, 8));
            else
                you.increase_duration(DUR_BARBS, random_range(2, 4), 12);

            if (you.attribute[ATTR_BARBS_POW])
            {
                you.attribute[ATTR_BARBS_POW] =
                    min(6, you.attribute[ATTR_BARBS_POW]++);
            }
            else
                you.attribute[ATTR_BARBS_POW] = 4;
        }
        // Insubstantial and jellies are immune
        else if (!(defender->is_insubstantial() &&
                    mons_genus(defender->type) != MONS_JELLY))
        {
            if (defender_visible)
            {
                mprf("%s %s skewered by barbed spikes.",
                     defender->name(DESC_THE).c_str(),
                     defender->conj_verb("are").c_str());
            }
            defender->as_monster()->add_ench(mon_enchant(ENCH_BARBS, 1,
                                        attacker,
                                        random_range(5, 7) * BASELINE_DELAY));
        }
        break;

    case AF_MINIPARA:
    {
        // Doesn't affect the poison-immune.
        if (defender->is_player() && you.duration[DUR_DIVINE_STAMINA] > 0)
        {
            mpr("Your divine stamina protects you from poison!");
            break;
        }
        if (defender->res_poison() >= 3)
            break;
        if (defender->res_poison() > 0 && !one_chance_in(3))
            break;
        defender->paralyse(attacker, 1);
        mons_do_poison();
        break;
    }

    case AF_POISON_PARALYSE:
    {
        // Doesn't affect the poison-immune.
        if (defender->is_player() && you.duration[DUR_DIVINE_STAMINA] > 0)
        {
            mpr("Your divine stamina protects you from poison!");
            break;
        }
        else if (defender->res_poison() >= 3)
            break;

        // Same frequency as AF_POISON and AF_POISON_STRONG.
        if (one_chance_in(3))
        {
            int dmg = random_range(attacker->get_hit_dice() * 3 / 2,
                                   attacker->get_hit_dice() * 5 / 2);
            defender->poison(attacker, dmg);
        }

        // Try to apply either paralysis or slowing, with the normal 2/3
        // chance to resist with rPois.
        if (one_chance_in(6))
        {
            if (defender->res_poison() <= 0 || one_chance_in(3))
                defender->paralyse(attacker, roll_dice(1, 3));
        }
        else if (defender->res_poison() <= 0 || one_chance_in(3))
            defender->slow_down(attacker, roll_dice(1, 3));

        break;
    }

    case AF_REACH_TONGUE:
    case AF_ACID:
        defender->splash_with_acid(attacker);
        break;

    case AF_CORRODE:
        defender->corrode_equipment(atk_name(DESC_THE).c_str());
        break;

    case AF_RIFT:
    case AF_DISTORT:
        distortion_affects_defender();
        break;

    case AF_RAGE:
        if (!one_chance_in(3) || !defender->can_go_berserk())
            break;

        if (needs_message)
        {
            mprf("%s %s %s!",
                 atk_name(DESC_THE).c_str(),
                 attacker->conj_verb("infuriate").c_str(),
                 defender_name(true).c_str());
        }

        defender->go_berserk(false);
        break;

    case AF_CHAOTIC:
        chaos_affects_defender();
        break;

    case AF_STEAL:
        // Ignore monsters, for now.
        if (!defender->is_player())
            break;

        attacker->as_monster()->steal_item_from_player();
        break;

    case AF_HOLY:
        if (defender->holy_wrath_susceptible())
            special_damage = attk_damage * 0.75;

        if (needs_message && special_damage)
        {
            mprf("%s %s %s%s",
                 atk_name(DESC_THE).c_str(),
                 attacker->conj_verb("sear").c_str(),
                 defender_name(true).c_str(),
                 attack_strength_punctuation(special_damage).c_str());

        }
        break;

    case AF_ANTIMAGIC:

        // Apply extra stacks of the effect to monsters that have none.
        if (defender->is_monster()
            && !defender->as_monster()->has_ench(ENCH_ANTIMAGIC))
        {
            antimagic_affects_defender(attacker->get_hit_dice() * 18);
        }
        else
            antimagic_affects_defender(attacker->get_hit_dice() * 12);

        if (mons_genus(attacker->type) == MONS_VINE_STALKER
            && attacker->is_monster())
        {
            const bool spell_user = defender->antimagic_susceptible();

            if (you.can_see(*attacker) || you.can_see(*defender))
            {
                mprf("%s drains %s %s.",
                     attacker->name(DESC_THE).c_str(),
                     defender->pronoun(PRONOUN_POSSESSIVE).c_str(),
                     spell_user ? "magic" : "power");
            }

            monster* vine = attacker->as_monster();
            if (vine->has_ench(ENCH_ANTIMAGIC)
                && (defender->is_player()
                    || (!defender->as_monster()->is_summoned()
                        && !mons_is_firewood(*defender->as_monster()))))
            {
                mon_enchant me = vine->get_ench(ENCH_ANTIMAGIC);
                vine->lose_ench_duration(me, random2(damage_done) + 1);
                simple_monster_message(*attacker->as_monster(),
                                       spell_user
                                       ? " looks very invigorated."
                                       : " looks invigorated.");
            }
        }
        break;

    case AF_PAIN:
        pain_affects_defender();
        break;

    case AF_ENSNARE:
        if (one_chance_in(3))
            ensnare(defender);
        break;

    case AF_CRUSH:
        if (needs_message)
        {
            mprf("%s %s %s.",
                 atk_name(DESC_THE).c_str(),
                 attacker->conj_verb("grab").c_str(),
                 defender_name(true).c_str());
        }
        attacker->start_constricting(*defender);
        // if you got grabbed, interrupt stair climb and passwall
        if (defender->is_player())
            stop_delay(true);
        break;

    case AF_ENGULF:
        if (x_chance_in_y(2, 3) && attacker->can_engulf(*defender))
        {
            const bool watery = attacker->type != MONS_QUICKSILVER_OOZE;
            if (defender->is_player() && !you.duration[DUR_WATER_HOLD])
            {
                you.duration[DUR_WATER_HOLD] = 10;
                you.props[WATER_HOLDER_KEY].get_int() = attacker->as_monster()->mid;
                you.props[WATER_HOLD_SUBSTANCE_KEY].get_string() = watery ? "water" : "ooze";
            }
            else if (defender->is_monster()
                     && !defender->as_monster()->has_ench(ENCH_WATER_HOLD))
            {
                defender->as_monster()->add_ench(mon_enchant(ENCH_WATER_HOLD, 1,
                                                             attacker, 1));
            }
            else
                return; //Didn't apply effect; no message

            if (needs_message)
            {
                mprf("%s %s %s%s!",
                     atk_name(DESC_THE).c_str(),
                     attacker->conj_verb("engulf").c_str(),
                     defender_name(true).c_str(),
                     watery ? " in water" : "");
            }
        }

        defender->expose_to_element(BEAM_WATER, 0);
        break;

    case AF_PURE_FIRE:
        if (attacker->type == MONS_FIRE_VORTEX)
            attacker->as_monster()->suicide(-10);

        special_damage = defender->apply_ac(base_damage, 0, ac_type::half);
        special_damage = resist_adjust_damage(defender,
                                              BEAM_FIRE,
                                              special_damage);

        if (needs_message && special_damage)
        {
            mprf("%s %s %s!",
                    atk_name(DESC_THE).c_str(),
                    attacker->conj_verb("burn").c_str(),
                    defender_name(true).c_str());

            _print_resist_messages(defender, special_damage, BEAM_FIRE);
        }

        defender->expose_to_element(BEAM_FIRE, 2);
        break;

    case AF_DRAIN_SPEED:
        if (x_chance_in_y(3, 5))
            drain_defender_speed();
        break;

    case AF_VULN:
        if (one_chance_in(3))
            defender->strip_willpower(attacker, 20 + random2(20), !needs_message);
        break;

    case AF_SHADOWSTAB:
        attacker->as_monster()->del_ench(ENCH_INVIS, true);
        break;

    case AF_DROWN:
        if (attacker->type == MONS_DROWNED_SOUL)
            attacker->as_monster()->suicide(-1000);

        if (!defender->res_water_drowning())
        {
            special_damage = base_damage;
            special_damage_flavour = BEAM_WATER;
            kill_type = KILLED_BY_WATER;

            if (needs_message)
            {
                mprf("%s %s %s%s",
                    atk_name(DESC_THE).c_str(),
                    attacker->conj_verb("drown").c_str(),
                    defender_name(true).c_str(),
                    attack_strength_punctuation(special_damage).c_str());
            }
        }
        break;

    case AF_WEAKNESS:
        if (coinflip())
            defender->weaken(attacker, 12);
        break;

    case AF_SEAR:
    {
        if (!one_chance_in(3))
            break;

        bool visible_effect = false;
        if (defender->is_player())
        {
            if (defender->res_fire() <= 3 && !you.duration[DUR_FIRE_VULN])
                visible_effect = true;
            you.increase_duration(DUR_FIRE_VULN, 3 + random2(attk_damage), 50);
        }
        else
        {
            if (!defender->as_monster()->has_ench(ENCH_FIRE_VULN))
                visible_effect = true;
            defender->as_monster()->add_ench(
                mon_enchant(ENCH_FIRE_VULN, 1, attacker,
                            (3 + random2(attk_damage)) * BASELINE_DELAY));
        }

        if (needs_message && visible_effect)
        {
            mprf("%s fire resistance is stripped away!",
                 def_name(DESC_ITS).c_str());
        }
        break;
    }

    case AF_SPIDER:
    {
        if (!one_chance_in(3))
            break;

        if (summon_spider(*attacker, defender->pos(),
                          attacker->as_monster()->god, SPELL_NO_SPELL,
                          attacker->get_hit_dice() * 12))
        {
            mpr("A spider bursts forth from the wound!");
        }
        break;
    }
    case AF_BLOODZERK:
    {
        if (!defender->can_bleed() || !attacker->can_go_berserk())
            break;

        monster* mon = attacker->as_monster();
        if (mon->has_ench(ENCH_MIGHT))
        {
            mon->del_ench(ENCH_MIGHT, true);
            mon->add_ench(mon_enchant(ENCH_BERSERK, 1, mon,
                                      random_range(100, 200)));
            simple_monster_message(*mon, " enters a blood-rage!");
        }
        else
        {
            mon->add_ench(mon_enchant(ENCH_MIGHT, 1, mon,
                                      random_range(100, 200)));
            simple_monster_message(*mon, " tastes blood and grows stronger!");
        }
        break;
    }
    case AF_SLEEP:
        if (!coinflip())
            break;
        if (attk_type == AT_SPORE)
        {
            if (defender->is_unbreathing())
                break;

            if (defender_visible)
            {
                mprf("%s %s engulfed in a cloud of soporific spores!",
                     defender->name(DESC_THE).c_str(),
                     defender->conj_verb("are").c_str());
            }
        }
        defender->put_to_sleep(attacker, attacker->get_experience_level() * 3);
        break;
    }
}

void melee_attack::do_fiery_armour_burn()
{
    if (!you.duration[DUR_FIERY_ARMOUR]
        || !attacker->alive()
        || !adjacent(you.pos(), attacker->pos()))
    {
        return;
    }

    bolt beam;
    beam.flavour = BEAM_FIRE;
    beam.thrower = KILL_YOU;

    monster* mon = attacker->as_monster();

    const int pre_ac = roll_dice(2, 4);
    const int post_ac = attacker->apply_ac(pre_ac);
    const int hurted = mons_adjust_flavoured(mon, beam, post_ac);

    if (!hurted)
        return;

    simple_monster_message(*mon, " is burned by your cloak of flames.");

    mon->hurt(&you, hurted);

    if (mon->alive())
    {
        mon->expose_to_element(BEAM_FIRE, post_ac);
        print_wounds(*mon);
    }
}

void melee_attack::do_passive_freeze()
{
    if (you.has_mutation(MUT_PASSIVE_FREEZE)
        && attacker->alive()
        && adjacent(you.pos(), attacker->as_monster()->pos()))
    {
        bolt beam;
        beam.flavour = BEAM_COLD;
        beam.thrower = KILL_YOU;

        monster* mon = attacker->as_monster();

        const int orig_hurted = random2(11);
        int hurted = mons_adjust_flavoured(mon, beam, orig_hurted);

        if (!hurted)
            return;

        simple_monster_message(*mon, " is very cold.");

        mon->hurt(&you, hurted);

        if (mon->alive())
        {
            mon->expose_to_element(BEAM_COLD, orig_hurted);
            print_wounds(*mon);
        }
    }
}

void melee_attack::mons_do_eyeball_confusion()
{
    if (you.has_mutation(MUT_EYEBALLS)
        && attacker->alive()
        && adjacent(you.pos(), attacker->as_monster()->pos())
        && x_chance_in_y(you.get_mutation_level(MUT_EYEBALLS), 20))
    {
        const int ench_pow = you.get_mutation_level(MUT_EYEBALLS) * 30;
        monster* mon = attacker->as_monster();

        if (mon->check_willpower(&you, ench_pow) <= 0)
        {
            mprf("The eyeballs on your body gaze at %s.",
                 mon->name(DESC_THE).c_str());

            if (!mon->clarity())
            {
                mon->add_ench(mon_enchant(ENCH_CONFUSION, 0, &you,
                                          30 + random2(100)));
            }
        }
    }
}

void melee_attack::mons_do_tendril_disarm()
{
    monster* mon = attacker->as_monster();

    if (you.get_mutation_level(MUT_TENDRILS)
        && one_chance_in(5)
        && !x_chance_in_y(mon->get_hit_dice(), 35))
    {
        item_def* mons_wpn = mon->disarm();
        if (mons_wpn)
        {
            mprf("Your tendrils lash around %s %s and pull it to the ground!",
                 apostrophise(mon->name(DESC_THE)).c_str(),
                 mons_wpn->name(DESC_PLAIN).c_str());
        }
    }
}

void melee_attack::do_spines()
{
    // Monsters only get struck on their first attack per round
    if (attacker->is_monster() && effective_attack_number > 0)
        return;

    if (defender->is_player())
    {
        const int mut = you.get_mutation_level(MUT_SPINY);

        if (mut && attacker->alive() && coinflip())
        {
            int dmg = random_range(mut,
                div_rand_round(you.experience_level * 2, 3) + mut * 3);
            int hurt = attacker->apply_ac(dmg);

            dprf(DIAG_COMBAT, "Spiny: dmg = %d hurt = %d", dmg, hurt);

            if (hurt <= 0)
                return;

            simple_monster_message(*attacker->as_monster(),
                                   " is struck by your spines.");

            attacker->hurt(&you, hurt);
        }
    }
    else if (defender->as_monster()->is_spiny())
    {
        // Thorn hunters can attack their own brambles without injury
        if (defender->type == MONS_BRIAR_PATCH
            && attacker->type == MONS_THORN_HUNTER
            // Dithmenos' shadow can't take damage, don't spam.
            || attacker->type == MONS_PLAYER_SHADOW
            // Don't let spines kill things out of LOS.
            || !summon_can_attack(defender->as_monster(), attacker))
        {
            return;
        }

        const bool cactus = defender->type == MONS_CACTUS_GIANT;
        if (attacker->alive() && (cactus || one_chance_in(3)))
        {
            const int dmg = spines_damage(defender->type).roll();
            int hurt = attacker->apply_ac(dmg);
            dprf(DIAG_COMBAT, "Spiny: dmg = %d hurt = %d", dmg, hurt);

            if (hurt <= 0)
                return;
            if (you.can_see(*defender) || attacker->is_player())
            {
                mprf("%s %s struck by %s %s.", attacker->name(DESC_THE).c_str(),
                     attacker->conj_verb("are").c_str(),
                     defender->name(DESC_ITS).c_str(),
                     defender->type == MONS_BRIAR_PATCH ? "thorns"
                                                        : "spines");
            }
            attacker->hurt(defender, hurt, BEAM_MISSILE, KILLED_BY_SPINES);
        }
    }
}

void melee_attack::emit_foul_stench()
{
    monster* mon = attacker->as_monster();

    if (you.has_mutation(MUT_FOUL_STENCH)
        && attacker->alive()
        && adjacent(you.pos(), mon->pos()))
    {
        const int mut = you.get_mutation_level(MUT_FOUL_STENCH);

        if (damage_done > 0 && x_chance_in_y(mut * 3 - 1, 20)
            && !cell_is_solid(mon->pos())
            && !cloud_at(mon->pos()))
        {
            mpr("You emit a cloud of foul miasma!");
            place_cloud(CLOUD_MIASMA, mon->pos(), 5 + random2(6), &you);
        }
    }
}

static int _minotaur_headbutt_chance()
{
    return 23 + you.experience_level;
}

void melee_attack::do_minotaur_retaliation()
{
    if (!defender->is_player())
    {
        // monsters have no STR or DEX
        if (x_chance_in_y(2, 5))
        {
            int hurt = attacker->apply_ac(random2(21));
            if (you.see_cell(defender->pos()))
            {
                const string defname = defender->name(DESC_THE);
                mprf("%s furiously retaliates!", defname.c_str());
                if (hurt <= 0)
                {
                    mprf("%s headbutts %s, but does no damage.", defname.c_str(),
                         attacker->name(DESC_THE).c_str());
                }
                else
                {
                    mprf("%s headbutts %s%s", defname.c_str(),
                         attacker->name(DESC_THE).c_str(),
                         attack_strength_punctuation(hurt).c_str());
                }
            }
            if (hurt > 0)
            {
                attacker->hurt(defender, hurt, BEAM_MISSILE,
                               KILLED_BY_HEADBUTT);
            }
        }
        return;
    }

    if (!form_keeps_mutations())
    {
        // You are in a non-minotaur form.
        return;
    }

    if (!x_chance_in_y(_minotaur_headbutt_chance(), 100))
        return;

    // Use the same damage formula as a regular headbutt.
    int dmg = AUX_HEADBUTT.get_damage(true);
    dmg = stat_modify_damage(dmg, SK_UNARMED_COMBAT, false);
    dmg = random2(dmg);
    dmg = apply_fighting_skill(dmg, true, true);
    dmg = player_apply_misc_modifiers(dmg);
    dmg = player_apply_slaying_bonuses(dmg, true);
    dmg = player_apply_final_multipliers(dmg, true);
    int hurt = attacker->apply_ac(dmg);
    hurt = player_apply_postac_multipliers(dmg);

    mpr("You furiously retaliate!");
    dprf(DIAG_COMBAT, "Retaliation: dmg = %d hurt = %d", dmg, hurt);
    if (hurt <= 0)
    {
        mprf("You headbutt %s, but do no damage.",
             attacker->name(DESC_THE).c_str());
        return;
    }
    mprf("You headbutt %s%s",
         attacker->name(DESC_THE).c_str(),
         attack_strength_punctuation(hurt).c_str());
    attacker->hurt(&you, hurt);
}

/** For UNRAND_STARLIGHT's dazzle effect, only against monsters.
 */
void melee_attack::do_starlight()
{
    static const vector<string> dazzle_msgs = {
        "@The_monster@ is blinded by the light from your cloak!",
        "@The_monster@ is temporarily struck blind!",
        "@The_monster@'s sight is seared by the starlight!",
        "@The_monster@'s vision is obscured by starry radiance!",
    };

    if (one_chance_in(5) && dazzle_monster(attacker->as_monster(), 100))
    {
        string msg = *random_iterator(dazzle_msgs);
        msg = do_mon_str_replacements(msg, *attacker->as_monster(), S_SILENT);
        mpr(msg);
    }
}


/**
 * Launch a counterattack against the attacker. No sanity checks;
 * caller beware!
 */
void melee_attack::riposte()
{
    if (you.see_cell(defender->pos()))
    {
        mprf("%s riposte%s.", defender->name(DESC_THE).c_str(),
             defender->is_player() ? "" : "s");
    }
    melee_attack attck(defender, attacker, 0, effective_attack_number + 1);
    attck.is_riposte = true;
    attck.attack();
}

bool melee_attack::do_knockback(bool slippery)
{
    if (defender->is_stationary())
        return false; // don't even print a message

    if (attacker->cannot_act())
        return false;

    const int size_diff =
        attacker->body_size(PSIZE_BODY) - defender->body_size(PSIZE_BODY);
    const coord_def old_pos = defender->pos();
    const coord_def new_pos = old_pos + old_pos - attack_position;

    if (!slippery && !x_chance_in_y(size_diff + 3, 6)
        // need a valid tile
        || !defender->is_habitable_feat(env.grid(new_pos))
        // don't trample anywhere the attacker can't follow
        || !attacker->is_habitable_feat(env.grid(old_pos))
        // don't trample into a monster - or do we want to cause a chain
        // reaction here?
        || actor_at(new_pos)
        // Prevent trample/drown combo when flight is expiring
        || defender->is_player() && need_expiration_warning(new_pos)
        || defender->is_constricted()
        || defender->resists_dislodge(needs_message ? "being knocked back" : ""))
    {
        if (needs_message)
        {
            if (defender->is_constricted())
            {
                mprf("%s %s held in place!",
                     defender_name(false).c_str(),
                     defender->conj_verb("are").c_str());
            }
            else if (!slippery)
            {
                mprf("%s %s %s ground!",
                     defender_name(false).c_str(),
                     defender->conj_verb("hold").c_str(),
                     defender->pronoun(PRONOUN_POSSESSIVE).c_str());
            }
        }

        return false;
    }

    if (needs_message)
    {
        const bool can_stumble = !defender->airborne()
                                  && !defender->incapacitated();
        const string verb = slippery ? "slip" :
                         can_stumble ? "stumble"
                                     : "are shoved";
        mprf("%s %s backwards!",
             defender_name(false).c_str(),
             defender->conj_verb(verb).c_str());
    }

    // Schedule following _before_ actually trampling -- if the defender
    // is a player, a shaft trap will unload the level. If trampling will
    // somehow fail, move attempt will be ignored.
    trample_follow_fineff::schedule(attacker, old_pos);

    if (defender->is_player())
    {
        move_player_to_grid(new_pos, false);
        // Interrupt stair travel and passwall.
        stop_delay(true);
    }
    else
        defender->move_to_pos(new_pos);

    return true;
}

bool melee_attack::do_drag()
{
    if (defender->is_stationary())
        return false; // don't even print a message

    if (attacker->cannot_act())
        return false;

    // Calculate what is 'backwards' from the attacker's perspective, relative
    // to the defender. (Remember, this can occur on reaching attacks, so
    // attacker and defender may be non-adjacent!)
    const coord_def drag_shift = -(defender->pos() - attacker->pos()).sgn();
    const coord_def new_defender_pos = defender->pos() + drag_shift;

    // Only move the attacker back if the defender was already adjacent and we
    // want to move them *into* the attacker's space.
    bool move_attacker = new_defender_pos == attacker->pos();
    coord_def new_attacker_pos = (move_attacker ? attacker->pos() + drag_shift
                                                : attacker->pos());

    // Abort if there isn't habitable empty space at the endpoints of both the
    // attacker and defender's move, or if something else is interfering with it.
    if (move_attacker && (!attacker->is_habitable(new_attacker_pos)
                         || actor_at(new_attacker_pos))
        || !defender->is_habitable(new_defender_pos)
        || (new_defender_pos != attacker->pos() && actor_at(new_defender_pos))
        || defender->is_player() && need_expiration_warning(new_defender_pos)
        || defender->is_constricted()
        || defender->resists_dislodge(needs_message ? "being dragged" : ""))
    {
        return false;
    }

    // We should be okay to move, then.
    if (needs_message)
    {
        mprf("%s drags %s backwards!",
             attacker->name(DESC_THE).c_str(),
             defender_name(true).c_str());
    }

    // Only move the attacker back if the defender was already adjacent and we
    // want to move them *into* the attacker's space.
    if (new_defender_pos == attacker->pos())
    {
        attacker->move_to_pos(new_attacker_pos);
        attacker->apply_location_effects(new_attacker_pos);
        attacker->did_deliberate_movement();
    }

    defender->move_to_pos(new_defender_pos);
    defender->apply_location_effects(new_defender_pos);
    defender->did_deliberate_movement();

    return true;
}

/**
 * Find the list of targets to cleave after hitting the main target.
 */
void melee_attack::cleave_setup()
{
    // Don't cleave on a self-attack attack, or on Manifold Assault.
    if (attacker->pos() == defender->pos() || is_projected)
        return;

    // We need to get the list of the remaining potential targets now because
    // if the main target dies, its position will be lost.
    get_cleave_targets(*attacker, defender->pos(), cleave_targets,
                       attack_number);
    // We're already attacking this guy.
    cleave_targets.pop_front();
}

// cleave damage modifier for additional attacks: 70% of base damage
int melee_attack::cleave_damage_mod(int dam)
{
    return div_rand_round(dam * 7, 10);
}

// Martial strikes get modified by momentum and maneuver specific damage mods.
int melee_attack::martial_damage_mod(int dam)
{
    if (wu_jian_has_momentum(wu_jian_attack))
        dam = div_rand_round(dam * 14, 10);

    if (wu_jian_attack == WU_JIAN_ATTACK_LUNGE)
        dam = div_rand_round(dam * 12, 10);

    if (wu_jian_attack == WU_JIAN_ATTACK_WHIRLWIND)
        dam = div_rand_round(dam * 8, 10);

    return dam;
}

void melee_attack::chaos_affect_actor(actor *victim)
{
    ASSERT(victim); // XXX: change to actor &victim
    melee_attack attk(victim, victim);
    attk.weapon = nullptr;
    attk.fake_chaos_attack = true;
    attk.chaos_affects_defender();
    if (!attk.special_damage_message.empty()
        && you.can_see(*victim))
    {
        mpr(attk.special_damage_message);
    }
}

/**
 * Does the player get to use the given aux attack during this melee attack?
 *
 * Partially random.
 *
 * @param atk   The type of aux attack being considered.
 * @return      Whether the player may use the given aux attack.
 */
bool melee_attack::_extra_aux_attack(unarmed_attack_type atk)
{
    ASSERT(atk >= UNAT_FIRST_ATTACK);
    ASSERT(atk <= UNAT_LAST_ATTACK);
    const AuxAttackType* const aux = aux_attack_types[atk - UNAT_FIRST_ATTACK];
    if (!x_chance_in_y(aux->get_chance(), 100))
        return false;

    if (wu_jian_attack != WU_JIAN_ATTACK_NONE
        && !x_chance_in_y(1, wu_jian_number_of_targets))
    {
       // Reduces aux chance proportionally to number of
       // enemies attacked with a martial attack
       return false;
    }

    // XXX: dedup with mut_aux_attack_desc()
    switch (atk)
    {
    case UNAT_CONSTRICT:
        return you.get_mutation_level(MUT_CONSTRICTING_TAIL) >= 2
                || you.has_mutation(MUT_TENTACLE_ARMS)
                    && you.has_usable_tentacle()
                || you.form == transformation::serpent;

    case UNAT_KICK:
        return you.has_usable_hooves()
               || you.has_usable_talons()
               || you.get_mutation_level(MUT_TENTACLE_SPIKE);

    case UNAT_PECK:
        return you.get_mutation_level(MUT_BEAK);

    case UNAT_HEADBUTT:
        return you.get_mutation_level(MUT_HORNS);

    case UNAT_TAILSLAP:
        // includes MUT_STINGER, MUT_ARMOURED_TAIL, MUT_WEAKNESS_STINGER, fishtail
        return you.has_tail()
               // constricting tails are too slow to slap
               && !you.has_mutation(MUT_CONSTRICTING_TAIL);

    case UNAT_PSEUDOPODS:
        return you.has_usable_pseudopods();

    case UNAT_TENTACLES:
        return you.has_usable_tentacles();

    case UNAT_BITE:
        return you.get_mutation_level(MUT_ANTIMAGIC_BITE)
               || (you.has_usable_fangs()
                   || you.get_mutation_level(MUT_ACIDIC_BITE));

    case UNAT_PUNCH:
        return player_gets_aux_punch();

    case UNAT_TOUCH:
        return you.get_mutation_level(MUT_DEMONIC_TOUCH)
               && you.has_usable_offhand();

    case UNAT_MAW:
        return you.form == transformation::maw;

    default:
        return false;
    }
}

bool melee_attack::using_weapon() const
{
    return weapon && is_melee_weapon(*weapon);
}

int melee_attack::weapon_damage() const
{
    if (!using_weapon())
        return 0;

    return property(*weapon, PWPN_DAMAGE);
}

int melee_attack::calc_mon_to_hit_base()
{
    const bool fighter = attacker->is_monster()
                         && attacker->as_monster()->is_fighter();
    return mon_to_hit_base(attacker->get_hit_dice(), fighter);
}

/**
 * Add modifiers to the base damage.
 * Currently only relevant for monsters.
 */
int melee_attack::apply_damage_modifiers(int damage)
{
    ASSERT(attacker->is_monster());
    monster *as_mon = attacker->as_monster();

    // Berserk/mighted monsters get bonus damage.
    if (as_mon->has_ench(ENCH_MIGHT) || as_mon->has_ench(ENCH_BERSERK))
        damage = damage * 3 / 2;

    if (as_mon->has_ench(ENCH_IDEALISED))
        damage *= 2; // !

    if (as_mon->has_ench(ENCH_WEAK))
        damage = damage * 2 / 3;

    // If the defender is asleep, the attacker gets a stab.
    if (defender && (defender->asleep()
                     || (attk_flavour == AF_SHADOWSTAB
                         &&!defender->can_see(*attacker))))
    {
        damage = damage * 5 / 2;
        dprf(DIAG_COMBAT, "Stab damage vs %s: %d",
             defender->name(DESC_PLAIN).c_str(),
             damage);
    }

    if (cleaving)
        damage = cleave_damage_mod(damage);

    return damage;
}

int melee_attack::calc_damage()
{
    // Constriction deals damage over time, not when grabbing.
    if (attk_flavour == AF_CRUSH)
        return 0;

    return attack::calc_damage();
}

/* TODO: This code is only used from melee_attack methods, but perhaps it
 * should be ambigufied and moved to the actor class
 * Should life protection protect from this?
 *
 * Should eventually remove in favour of player/monster symmetry
 *
 * Called when stabbing and for bite attacks.
 *
 * Returns true if blood was drawn.
 */
bool melee_attack::_player_vampire_draws_blood(const monster* mon, const int damage,
                                               bool needs_bite_msg)
{
    ASSERT(you.has_mutation(MUT_VAMPIRISM));

    if (!_vamp_wants_blood_from_monster(mon) ||
        (!adjacent(defender->pos(), attack_position) && needs_bite_msg))
    {
        return false;
    }

    // Now print message, need biting unless already done (never for bat form!)
    if (needs_bite_msg && you.form != transformation::bat)
    {
        mprf("You bite %s, and draw %s blood!",
             mon->name(DESC_THE, true).c_str(),
             mon->pronoun(PRONOUN_POSSESSIVE).c_str());
    }
    else
    {
        mprf("You draw %s blood!",
             apostrophise(mon->name(DESC_THE, true)).c_str());
    }

    // Regain hp.
    if (you.hp < you.hp_max)
    {
        int heal = 2 + random2(damage);
        heal += random2(damage);
        if (heal > you.experience_level)
            heal = you.experience_level;

        if (heal > 0 && !you.duration[DUR_DEATHS_DOOR])
        {
            inc_hp(heal);
            canned_msg(MSG_GAIN_HEALTH);
        }
    }

    return true;
}

bool melee_attack::apply_damage_brand(const char *what)
{
    // Staff damage overrides any brands
    return apply_staff_damage() || attack::apply_damage_brand(what);
}

bool melee_attack::_vamp_wants_blood_from_monster(const monster* mon)
{
    return you.has_mutation(MUT_VAMPIRISM)
           && !you.vampire_alive
           && actor_is_susceptible_to_vampirism(*mon)
           && mons_has_blood(mon->type);
}

string mut_aux_attack_desc(mutation_type mut)
{
    // XXX: dedup with _extra_aux_attack()
    switch (mut)
    {
    case MUT_HOOVES:
    case MUT_TALONS:
    case MUT_TENTACLE_SPIKE:
        return AUX_KICK.describe();
    case MUT_BEAK:
        return AUX_PECK.describe();
    case MUT_HORNS:
        return AUX_HEADBUTT.describe();
    case MUT_STINGER:
    case MUT_ARMOURED_TAIL:
    case MUT_WEAKNESS_STINGER:
    case MUT_MERTAIL:
        return AUX_TAILSLAP.describe();
    case MUT_ACIDIC_BITE:
    case MUT_ANTIMAGIC_BITE:
    case MUT_FANGS:
        return AUX_BITE.describe();
    case MUT_DEMONIC_TOUCH:
        return AUX_TOUCH.describe();
    case MUT_REFLEXIVE_HEADBUTT:
        return make_stringf("\nTrigger chance:  %d%%\n"
                              "Base damage:     %d\n\n",
                            _minotaur_headbutt_chance(),
                            AUX_HEADBUTT.get_damage(false));
    default:
        return "";
    }
}

static string _desc_aux(int chance, int to_hit, int dam)
{
    string to_hit_pips = "";
    // Each pip is 10 to-hit. Since to-hit is rolled before we compare it to
    // defender evasion, for these pips to be comparable to monster EV pips,
    // these need to be twice as large.
    for (int i = 0; i < to_hit / 10; ++i)
    {
        to_hit_pips += "+";
        if (i % 5 == 4)
            to_hit_pips += " ";
    }
    return make_stringf("\nTrigger chance:  %d%%\n"
                          "Accuracy:        %s\n"
                          "Base damage:     %d",
                        chance,
                        to_hit_pips.c_str(),
                        dam);
}

string aux_attack_desc(unarmed_attack_type unat, int force_damage)
{
    const unsigned long idx = unat - UNAT_FIRST_ATTACK;
    ASSERT_RANGE(idx, 0, ARRAYSZ(aux_attack_types));
    const AuxAttackType* const aux = aux_attack_types[idx];
    const int dam = force_damage == -1 ? aux->get_damage(false) : force_damage;
    // lazily assume chance and to hit don't vary in/out of forms
    return _desc_aux(aux->get_chance(), aux_to_hit(), dam);
}

string AuxAttackType::describe() const
{
    return _desc_aux(get_chance(), aux_to_hit(), get_damage(false)) + "\n\n";
}
