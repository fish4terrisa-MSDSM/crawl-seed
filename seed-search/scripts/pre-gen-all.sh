#!/usr/bin/env bash
INDEX=`cat $GITHUB_WORKSPACE/index`
START=$((INDEX + 1))
END=$((START + 9999))
echo ${END} > $GITHUB_WORKSPACE/index
#set -x
if [ -z "$CRAWL_PATH" ] ; then
    CRAWL_PATH=$GITHUB_WORKSPACE/crawl
fi
mkdir -p output
SEED=${START}
while (( $(echo "${SEED} <= ${END}" |bc -l) ));
do
cache="output/seed-${SEED}.jsonl"
if [ ! -f "$cache" ] ; then
    cp -f scripts/scrape-seed-7.lua "$CRAWL_PATH"/source/scripts/scrape-seed-7.lua 2>&1 >/dev/null
    "$GITHUB_WORKSPACE/crawl/source/util/fake_pty" $GITHUB_WORKSPACE/crawl/source/crawl -script scrape-seed-7.lua "$SEED" 8 2>&1 | sed '/^$/d' > "$cache"
fi
echo "$SEED"
if (grep -q "MP+" $cache && ( grep -q "Int+6" $cache || grep -q "Int+7" $cache || grep -q "Int+8" $cache || grep -q "Int+9" $cache || grep -q "Int+10" $cache || grep -q "Int+11" $cache || grep -q "Int+12" $cache ) && grep -q "rElec" $cache && (grep -q "Arcjolt" $cache || grep -q "Plasma Beam" $cache)  ) ; then
#if (grep -q "club" $cache && grep -q "D:1" $cache) ; then
	echo "meet need"
else
	echo "no"
	rm $cache
fi
((SEED++))
done
