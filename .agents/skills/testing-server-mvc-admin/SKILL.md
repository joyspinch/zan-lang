---
name: testing-server-mvc-admin
description: How to bring up a server-mvc template instance and browser-test its admin pages (especially /admin/monitor 运行监控) on this box.
---

# Testing the `server-mvc` template admin UI

## Bringing up an instance
- Template source lives in `templates/server/server-mvc`; a runnable instance is normally
  scaffolded/copied into `_scratch/<name>` and built with the compiler in `build*/`.
- Start it detached so it survives the shell:
  `cd _scratch/<name> && (setsid ./<binary> > run.log 2>&1 &)`
- The metrics/session stack needs redis: `sudo service redis-server start`.
- Default listen address for scratch instances used so far: `http://127.0.0.1:8099`.
- Master + N workers all share the same binary name, so `pgrep -f <dir>/<binary>` lists them;
  worker 0 in the 进程 table is the master (0 requests). Killing a worker with `kill -9` is safe —
  the master respawns it with a new pid within a couple of seconds.

## Logging in
- `/admin` redirects to `/admin/login`; form fields are `user` / `pass`.
  Seeded dev credentials in scratch instances: `admin` / `admin1234`.
- The admin shell is a tabbed SPA (`wwwroot/js/admin.js`): links marked `data-load` swap the panel in
  place without a full navigation, and `[data-search]` forms submit via fetch. So the URL bar does
  change, but the page is not reloaded — don't expect a fresh document per click.

## Monitor page (运行监控, `/admin/monitor`) specifics
- Two independent data paths, and knowing which is which is essential to judge "numbers jumped":
  - **Day/cumulative half** — server-rendered on first paint (`Monitor.zan` `Index`), then polled
    from `/admin/monitor/today?d=YYYY-MM-DD` **once every 60 s** (`admin.js` `startDay`). So after
    generating traffic you must wait up to a full minute before the top KPIs and the two
    "每 5 分钟" day charts move. Don't call it a bug before a tick has passed.
  - **Live half** — SSE `/admin/monitor/stream` (`metrics` + `series` events) drives the four
    per-second charts, the 进程 table, the SQL ranking, and the `failed`/`rejected` columns. These
    are per-live-worker counters and legitimately reset when a worker restarts.
- Day buckets are merged by bucket timestamp into `dayPts`, never assigned, which is what makes the
  day charts unable to shrink. If you ever see a day chart get shorter or a day KPI drop, that is a
  real regression — look at `admin.js` `paintDay` and `MetricsStore.WindowTotals`.
- 平均耗时 is an average, so it *can* legitimately fall (a burst of fast requests pulls it down).
  Only counters (请求 / 错误) and 最慢请求 are expected to be monotonic within a day.
- Date box (`input[name=d]`, type=date — type it as `MM/DD/YYYY` in a US-locale Chrome) + 「查看」
  loads a past day; 「回到今天」 only renders when the picked day < today (`past` flag);
  「按时段细看」 goes to `/admin/monitor/history?d=...` which buckets the day by hour.
- Handy no-auth traffic generator for watching the charts:
  `for i in $(seq 1 200); do curl -s -o /dev/null http://127.0.0.1:8099/; done`
- Known-benign console noise: `GET /favicon.ico 404 (Not Found)`. Anything else in the console is
  worth reporting.

## Seeding history
Past-day figures come from the metrics DB, so to test "查看某一天" you need rows for that date in the
metrics store; without seeded data the day will legitimately show 0 and an empty curve.

## Devin Secrets Needed
None — everything runs locally with seeded dev credentials.
