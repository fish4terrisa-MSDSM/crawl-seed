/**
 * @file
 * @brief Functions with decks of cards.
 *
 * Decks are now a Nemelex exclusive abstraction. This code isn't in the
 * religion files because in past versions decks were an item that could be
 * interacted with by any character, spawn on the ground, etc.
**/

#pragma once

#include "deck-type.h"
#include "ability-type.h"
#include "enum.h"
#include "spl-cast.h"
#include "tag-version.h"

#define NEMELEX_TRIPLE_DRAW_KEY "nemelex_triple_draw"
#define NEMELEX_STACK_KEY       deck_name(DECK_STACK)

/// The minimum number of cards to deal when gifting.
const int MIN_GIFT_CARDS = 4;
/// The maximum number of cards to deal when gifting.
const int MAX_GIFT_CARDS = 13;

enum card_type
{
    CARD_VELOCITY,            // remove slow, alter others' speeds
    CARD_TOMB,                // a ring of rock walls
    CARD_EXILE,               // banish others, maybe self
#if TAG_MAJOR_VERSION == 34
    CARD_SHAFT_REMOVED,       // removed
#endif
    CARD_VITRIOL,             // acid damage
    CARD_CLOUD,               // encage enemies in rings of clouds
    CARD_STORM,               // wind and rain
    CARD_PAIN,                // necromancy, manipulating life itself
    CARD_TORMENT,             // symbol of
    CARD_ORB,                 // pure bursts of energy
    CARD_ELIXIR,              // restoration of hp and mp
    CARD_SUMMON_DEMON,        // dual demons
    CARD_SUMMON_WEAPON,       // a dance partner
    CARD_SUMMON_BEE,          // swarm of bees
    CARD_WILD_MAGIC,          // miscasts for everybody
#if TAG_MAJOR_VERSION == 34
    CARD_STAIRS_REMOVED,      // moved stairs around
#endif
    CARD_WRATH,               // random godly wrath
    CARD_WRAITH,              // drain XP
#if TAG_MAJOR_VERSION == 34
    CARD_FAMINE_REMOVED,      // starving
#endif
    CARD_SWINE,               // *oink*
    CARD_ILLUSION,            // a copy of the player
    CARD_DEGEN,               // polymorph hostiles down hd, malmutate
    CARD_ELEMENTS,            // primal animals of the elements
    CARD_RANGERS,             // sharpshooting
    NUM_CARDS
};

const char* card_name(card_type card);
card_type name_to_card(string name);
const string deck_contents(deck_type deck);
int deck_cards(deck_type deck);
string which_decks(card_type card);
const string deck_flavour(deck_type deck);
deck_type ability_deck(ability_type abil);

bool gift_cards();
void reset_cards();
string deck_summary();

bool deck_draw(deck_type deck);
spret deck_triple_draw(bool fail);
spret deck_deal(bool fail);
spret deck_stack(bool fail);

bool draw_three();
bool stack_five(int slot);

void card_effect(card_type which_card, bool dealt = false,
        bool punishment = false,
        bool tell_card = true);
void draw_from_deck_of_punishment(bool deal = false);

string deck_status(deck_type deck);
string deck_name(deck_type deck);
string deck_description(deck_type deck);
const string stack_top();
const string stack_contents();
bool card_is_removed(card_type card);

#if TAG_MAJOR_VERSION == 34
bool is_deck_type(uint8_t type);
bool is_deck(const item_def &item);
void reclaim_decks();
void reclaim_decks_on_level();
#endif
