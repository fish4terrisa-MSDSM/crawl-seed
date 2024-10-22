#!/usr/bin/env bash
if [ "$#" -ne 0 ] ; then
    SEED="$1"
else
    SEED=1
fi

if [ -z "$CRAWL_PATH" ] ; then
    CRAWL_PATH=/usr/src/crawl
fi
mkdir -p cache
cache="cache/seed-${SEED}.jsonl.gz"
if [ ! -f "$cache" ] ; then
    cp -f scripts/scrape-seed.lua "$CRAWL_PATH"/source/scripts/scrape-seed.lua 2>&1 >/dev/null
    '/usr/src/crawl/source/util/fake_pty' /usr/src/crawl/source/crawl -script scrape-seed.lua "$SEED" 2>&1 | sed '/^$/d' | gzip > "$cache"
fi
echo "$SEED"
