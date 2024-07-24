/**
 * @file
 * @brief Misc functions.
**/

#pragma once

#include "coord.h"

#include <algorithm>
#include <chrono>
#include <vector>

using std::vector;

void swap_with_monster(monster *mon_to_swap);

void handle_real_time(chrono::time_point<chrono::system_clock> when
                      = chrono::system_clock::now());

bool december_holidays();
bool today_is_halloween();
bool today_is_serious();
bool now_is_morning();

unsigned int breakpoint_rank(int val, const int breakpoints[],
                             unsigned int num_breakpoints);

struct counted_monster_list
{
    counted_monster_list() { };
    counted_monster_list(vector<monster *> ms);
    typedef pair<const monster* ,int> counted_monster;
    typedef vector<counted_monster> counted_list;
    counted_list list;
    void add(const monster* mons);
    int count();
    bool empty() { return list.empty(); }
    string describe(description_level_type desc = DESC_THE);
};
