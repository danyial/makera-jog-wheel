# makera-handrad

ESP32 firmware for a Wi-Fi handwheel that jogs a Makera Z1 Pro CNC machine (mill). Full specification: `SPEC.md`. In case of conflict, `SPEC.md` wins.

## Non-negotiable

- Never commit or push to `main`. `main` is protected on the server.
- No work without a ticket. One ticket = one branch = one merge request. Do not fix unrelated bugs on the side; create a new ticket instead.
- Branch names: `feature/<ticket>-<slug>` or `bugfix/<ticket>-<slug>`, lowercase.
- Merge only via merge request, only with a green pipeline and after review by a fresh context.
- Safety before convenience: the machine must never move unexpectedly. Never weaken the jog gate, planner or overrun window (SPEC 8.5–8.7) to work around a problem.
- `lib/core` includes nothing except the C++ standard library. GPIO numbers only in `include/pins.h`, logic constants only in `lib/core/core_config.h`.
- Never tick [MANUAL] items in `HARDWARE_CHECKLIST.md` yourself. Only the human checks hardware.
- Do not ask open questions; decide with a rationale and record it in `DECISIONS.md`.
- Features and bugfixes are always implemented by a subagent. The main context plans (ticket, requirements, acceptance criteria), gives the subagent a complete briefing, checks the result, has the diff reviewed by a fresh context and takes care of MR, pipeline and merge. Safety-relevant parts (SPEC ch. 15, right column) are only ever worked on by one subagent at a time, never several in parallel.
- Documentation (Markdown, SPEC) is written in English. Text the firmware shows (display, web UI, serial output) stays German and is quoted verbatim in the docs.

## Build

    pio run -e firebeetle
    pio test -e native

## Where to find what

- Building, testing, flashing, typical errors → skill `build-test`
- Architecture, protocol, domain logic, order of work → `SPEC.md`
