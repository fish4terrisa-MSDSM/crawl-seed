#pragma once

#include <list>

#include "attack.h"
#include "fight.h"
#include "random-var.h"
#include "tag-version.h"

enum unarmed_attack_type
{
    UNAT_NO_ATTACK,                    //    0
    UNAT_CONSTRICT,  // put constriction first so octopodes will use it
    UNAT_KICK,
    UNAT_HEADBUTT,
    UNAT_PECK,
    UNAT_TAILSLAP,
    UNAT_TOUCH,
    UNAT_PUNCH,
    UNAT_BITE,
    UNAT_PSEUDOPODS,
    UNAT_TENTACLES,
    UNAT_MAW,
    UNAT_FIRST_ATTACK = UNAT_CONSTRICT,
    UNAT_LAST_ATTACK = UNAT_MAW,
    NUM_UNARMED_ATTACKS,
};

class melee_attack : public attack
{
public:
    // mon_attack_def stuff
    int       attack_number;
    int       effective_attack_number;

    list<actor*> cleave_targets;
    bool         cleaving;        // additional attack from cleaving
    bool         is_multihit;     // quick blade follow-up attack
    bool         is_riposte;      // fencers' retaliation attack
    bool         is_projected;    // projected weapon spell attack
    int          charge_pow;      // electric charge bonus damage
    bool         never_cleave;    // if this attack shouldn't trigger cleave
                                  // followups, but still do 100% damage
    wu_jian_attack_type wu_jian_attack;
    int wu_jian_number_of_targets;
    coord_def attack_position;

public:
    melee_attack(actor *attacker, actor *defender,
                 int attack_num = -1, int effective_attack_num = -1,
                 bool is_cleaving = false);

    // Applies attack damage and other effects.
    bool attack();
    int calc_to_hit(bool random) override;
    int post_roll_to_hit_modifiers(int mhit, bool random) override;

    bool would_prompt_player();

    static void chaos_affect_actor(actor *victim);

private:
    /* Attack phases */
    bool handle_phase_attempted() override;
    bool handle_phase_dodged() override;
    bool handle_phase_hit() override;
    bool handle_phase_damaged() override;
    bool handle_phase_aux(); // specific to melee attacks
    bool handle_phase_killed() override;
    bool handle_phase_end() override;

    /* Combat Calculations */
    bool using_weapon() const override;
    int weapon_damage() const override;
    int calc_mon_to_hit_base() override;
    int apply_damage_modifiers(int damage) override;
    int calc_damage() override;
    bool apply_damage_brand(const char *what = nullptr) override;

    /* Attack effects */
    void check_autoberserk();
    bool check_unrand_effects() override;

    void rot_defender(int amount);

    bool consider_decapitation(int damage_done, int damage_type = -1);
    bool attack_chops_heads(int damage_done, int damage_type);
    void decapitate(int dam_type);

    /* Axe cleaving */
    void cleave_setup();
    int cleave_damage_mod(int dam);

    /* Long blade riposte */
    void riposte();

    /* Wu Jian martial attacks*/
    int martial_damage_mod(int dam);

    /* Mutation Effects */
    void do_spines();
    void do_passive_freeze();
#if TAG_MAJOR_VERSION == 34
    void do_passive_heat();
#endif
    void emit_foul_stench();

    /* Divine Effect */
    void do_fiery_armour_burn();

    /* Retaliation Effects */
    void do_minotaur_retaliation();
    void maybe_riposte();

    /* Item Effects */
    void do_starlight();

    /* Brand / Attack Effects */
    bool do_knockback(bool slippery);
    bool do_drag();

    /* Output methods */
    void set_attack_verb(int damage) override;
    void announce_hit() override;
private:
    // Monster-attack specific stuff
    bool mons_attack_effects() override;
    void mons_apply_attack_flavour();
    string mons_attack_verb();
    string mons_attack_desc();
    // TODO: Unify do_poison and poison_monster
    bool mons_do_poison();
    void mons_do_napalm();
    void mons_do_eyeball_confusion();
    void mons_do_tendril_disarm();
    void apply_black_mark_effects();
    void do_ooze_engulf();
private:
    // Player-attack specific stuff
    // Auxiliary unarmed attacks.
    bool player_aux_unarmed();
    bool player_gets_aux_punch();
    void player_aux_setup(unarmed_attack_type atk);
    bool player_aux_test_hit();
    bool player_aux_apply(unarmed_attack_type atk);

    int  player_apply_misc_modifiers(int damage) override;
    int  player_apply_final_multipliers(int damage, bool aux = false) override;
    int  player_apply_postac_multipliers(int damage) override;

    void player_exercise_combat_skills() override;
    bool player_monattk_hit_effects();
    void attacker_sustain_passive_damage();
    int  staff_damage(skill_type skill);
    bool apply_staff_damage();
    void player_stab_check() override;
    bool player_good_stab() override;
    void player_announce_aux_hit();
    string charge_desc();
    void player_warn_miss();
    void player_weapon_upsets_god();
    bool bad_attempt();
    bool player_unrand_bad_attempt(bool check_only = false);
    void _defender_die();

    // Added in, were previously static methods of fight.cc
    bool _extra_aux_attack(unarmed_attack_type atk);
    bool _player_vampire_draws_blood(const monster* mon, const int damage,
                                     bool needs_bite_msg = false);
    bool _vamp_wants_blood_from_monster(const monster* mon);

    bool can_reach();
};

string aux_attack_desc(unarmed_attack_type unat, int force_damage = -1);
string mut_aux_attack_desc(mutation_type mut);
