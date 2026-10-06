#include "target_selection.h"
#include <cstdio>
#include <limits>

int main() {
    using namespace third_person;
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        if (!ok) { std::fprintf(stderr, "FAIL: %s\n", name); ++failures; }
    };
    TargetSelector selector;
    TargetCandidate c[] = {
        {{1,10,1}, .1f,0,10,true,true,true,true,true},
        {{1,20,1}, .2f,0,5,true,true,true,true,true},
    };
    auto update = [&](uint64_t time, bool toggle = false) {
        return selector.update(c,2,1,time,toggle,true);
    };
    check(update(1).selected.id == 10, "closest to screen center, not distance");
    c[1].screenX = .08f;
    check(update(2).selected.id == 10, "small changes retain soft target");
    c[1].screenX = .01f;
    check(update(3).selected.id == 20, "significantly better target switches");
    auto r = update(4,true);
    check(r.locked && r.selected.id == 20, "middle press locks");
    c[1].screenX = .9f; c[0].screenX = 0;
    check(update(5).selected.id == 20, "lock ignores center competition");
    c[1].visible = false;
    r = update(200);
    check(r.locked && !r.attackAllowed, "brief occlusion retains lock but blocks attack");
    r = update(306);
    check(!r.locked && r.selected.id == 10, "occlusion timeout releases lock");
    r = update(307,true);
    check(r.locked && r.selected.id == 10, "lock replacement");
    check(!update(308,true).locked, "second press unlocks");
    check(update(309,true).locked, "relock");
    c[0].alive = false;
    r = update(310);
    check(!r.locked && !r.selected.valid(), "dead target invalidated");
    c[0].alive = true;
    check(update(311,true).locked, "lock live target");
    c[0].key.generation = 2;
    check(!update(312).locked, "recycled ID is not old locked enemy");
    c[0].hostile = false;
    check(!update(313).selected.valid(), "friendly excluded");
    c[0].hostile = true; c[0].inFront = false;
    check(!update(314).selected.valid(), "behind camera excluded");
    c[0].inFront = true; c[0].targetable = false;
    check(!update(315).selected.valid(), "untargetable excluded");
    c[0].targetable = true; c[0].distance = 100;
    check(!update(316).selected.valid(), "distant excluded");
    c[0].distance = std::numeric_limits<float>::quiet_NaN();
    check(!update(317).selected.valid(), "invalid distance excluded");
    c[0].distance = 10; c[0].screenX = std::numeric_limits<float>::infinity();
    check(!update(318).selected.valid(), "invalid projection excluded");
    c[0].screenX = 0;
    check(update(319,true).locked, "lock before deactivate");
    check(!selector.update(c,2,1,320,false,false).selected.valid(), "UI deactivate clears target");
    check(!update(321).locked, "no stale lock on reactivation");
    update(322,true);
    check(!selector.update(c,2,2,323,false,true).selected.valid(), "new session rejects old units");
    check(!selector.update(nullptr,2,1,324,false,true).selected.valid(), "missing snapshots fail closed");
    selector.reset();
    c[1] = c[0]; c[1].key.id = 5;
    check(update(325).selected.id == 5, "deterministic tie");
    update(326,true);
    check(!selector.update(nullptr,0,1,327,false,true).locked, "disappeared unit unlocks");
    return failures ? 1 : 0;
}
