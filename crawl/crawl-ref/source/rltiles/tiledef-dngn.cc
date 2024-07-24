// This file has been automatically generated.

#include "AppHdr.h"
#include "tiledef-dngn.h"

unsigned int tile_dngn_count(tileidx_t idx)
{
    if (idx < TILE_FLOOR_MAX)
    {
        return (tile_floor_count(idx));
    }
    else if (idx < TILE_WALL_MAX)
    {
        return (tile_wall_count(idx));
    }
    else
    {
        ASSERT(idx < TILE_FEAT_MAX);
        return (tile_feat_count(idx));
    }
}

tileidx_t tile_dngn_basetile(tileidx_t idx)
{
    if (idx < TILE_FLOOR_MAX)
    {
        return (tile_floor_basetile(idx));
    }
    else if (idx < TILE_WALL_MAX)
    {
        return (tile_wall_basetile(idx));
    }
    else
    {
        ASSERT(idx < TILE_FEAT_MAX);
        return (tile_feat_basetile(idx));
    }
}

int tile_dngn_probs(tileidx_t idx)
{
    if (idx < TILE_FLOOR_MAX)
    {
        return (tile_floor_probs(idx));
    }
    else if (idx < TILE_WALL_MAX)
    {
        return (tile_wall_probs(idx));
    }
    else
    {
        ASSERT(idx < TILE_FEAT_MAX);
        return (tile_feat_probs(idx));
    }
}

int tile_dngn_dominoes(tileidx_t idx)
{
    if (idx < TILE_FLOOR_MAX)
    {
        return (tile_floor_dominoes(idx));
    }
    else if (idx < TILE_WALL_MAX)
    {
        return (tile_wall_dominoes(idx));
    }
    else
    {
        ASSERT(idx < TILE_FEAT_MAX);
        return (tile_feat_dominoes(idx));
    }
}

const char *tile_dngn_name(tileidx_t idx)
{
    if (idx < TILE_FLOOR_MAX)
    {
        return (tile_floor_name(idx));
    }
    else if (idx < TILE_WALL_MAX)
    {
        return (tile_wall_name(idx));
    }
    else
    {
        ASSERT(idx < TILE_FEAT_MAX);
        return (tile_feat_name(idx));
    }
}

tile_info &tile_dngn_info(tileidx_t idx)
{
    if (idx < TILE_FLOOR_MAX)
    {
        return (tile_floor_info(idx));
    }
    else if (idx < TILE_WALL_MAX)
    {
        return (tile_wall_info(idx));
    }
    else
    {
        ASSERT(idx < TILE_FEAT_MAX);
        return (tile_feat_info(idx));
    }
}

bool tile_dngn_index(const char *str, tileidx_t *idx)
{
    if (tile_floor_index(str, idx))
        return true;
    if (tile_wall_index(str, idx))
        return true;
    if (tile_feat_index(str, idx))
        return true;
    return false;
}

bool tile_dngn_equal(tileidx_t idx, tileidx_t cmp)
{
    if (idx < TILE_FLOOR_MAX)
    {
        return (tile_floor_equal(idx, cmp));
    }
    else if (idx < TILE_WALL_MAX)
    {
        return (tile_wall_equal(idx, cmp));
    }
    else
    {
        ASSERT(idx < TILE_FEAT_MAX);
        return (tile_feat_equal(idx, cmp));
    }
}

tileidx_t tile_dngn_coloured(tileidx_t idx, int col)
{
    if (idx < TILE_FLOOR_MAX)
    {
        return (tile_floor_coloured(idx, col));
    }
    else if (idx < TILE_WALL_MAX)
    {
        return (tile_wall_coloured(idx, col));
    }
    else
    {
        ASSERT(idx < TILE_FEAT_MAX);
        return (tile_feat_coloured(idx, col));
    }
}

