define(["jquery","./tileinfo-floor", "./tileinfo-wall", "./tileinfo-feat", ],
       function ($, floor, wall, feat) {
// This file has been automatically generated.

var exports = {};
$.extend(exports, floor);
$.extend(exports, wall);
$.extend(exports, feat);

var val = 0;
exports.DNGN_MAX = window.TILE_DNGN_MAX = feat.FEAT_MAX;

exports.get_tile_info = function (idx)
{
    if (idx < floor.FLOOR_MAX)
    {
        return (floor.get_tile_info(idx));
    }
    else if (idx < wall.WALL_MAX)
    {
        return (wall.get_tile_info(idx));
    }
    else
    {
        assert(idx < feat.FEAT_MAX);
        return (feat.get_tile_info(idx));
    }
};

exports.tile_count = function (idx)
{
    if (idx < floor.FLOOR_MAX)
    {
        return (floor.tile_count(idx));
    }
    else if (idx < wall.WALL_MAX)
    {
        return (wall.tile_count(idx));
    }
    else
    {
        assert(idx < feat.FEAT_MAX);
        return (feat.tile_count(idx));
    }
};

exports.basetile = function (idx)
{
    if (idx < floor.FLOOR_MAX)
    {
        return (floor.basetile(idx));
    }
    else if (idx < wall.WALL_MAX)
    {
        return (wall.basetile(idx));
    }
    else
    {
        assert(idx < feat.FEAT_MAX);
        return (feat.basetile(idx));
    }
};

exports.get_img = function (idx) {
    if (idx < floor.FLOOR_MAX)
    {
        return "floor";
    }
    else if (idx < wall.WALL_MAX)
    {
        return "wall";
    }
    else
    {
        assert(idx < feat.FEAT_MAX);
        return "feat";
    }
};

return exports;
});
