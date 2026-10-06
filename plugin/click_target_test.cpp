#include "click_target.h"
#include <cstdio>
#include <cstdlib>
using namespace click_target;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
Candidate enemy(unsigned id = 7, unsigned life = 1, unsigned session = 10) {
    return {{session, life, id}, true, true};
}
void select(State& s, Candidate c = enemy()) {
    s.update({10, true, true});
    CHECK(s.leftDown({PickKind::Monster, c}) == ClickResult::Consume);
    CHECK(s.leftUp() == ClickResult::Consume);
}
int main() {
    State s;
    select(s);
    CHECK(s.target() == enemy().identity);
    CHECK(!s.manualAim(enemy(), true, true, true)); // Shift still held.
    s.update({10, true, false});
    CHECK(s.target() == enemy().identity);
    CHECK(!s.manualAim(enemy(), false, true, true)); // Never autoattack.
    CHECK(!s.manualAim(enemy(), true, false, true));
    CHECK(!s.manualAim(enemy(), true, true, false));
    CHECK(s.target().valid()); // Temporary range/LOS failure retains selection.
    CHECK(s.leftDown({}) == ClickResult::PassNative);
    CHECK(s.manualAim(enemy(), true, true, true)); // Ordinary manual left attack.
    CHECK(s.leftUp() == ClickResult::PassNative);
    select(s, enemy(8));
    CHECK(s.target() == enemy(8).identity);
    s.validate(enemy(8, 2)); // Recycled ID cannot inherit selection.
    CHECK(!s.target().valid());
    s.validate(enemy(8));
    CHECK(!s.target().valid());
    for (int reason = 0; reason < 4; ++reason) {
        select(s);
        auto c = enemy();
        if (reason == 0) c.liveHostileMonster = false;
        if (reason == 1) c.targetable = false;
        if (reason == 2) c.identity.session++;
        if (reason == 3) c.identity.lifetime = 0;
        s.validate(c);
        CHECK(!s.target().valid());
    }
    select(s);
    s.update({10, true, true});
    CHECK(s.leftDown({PickKind::Monster, enemy(9)}) == ClickResult::Consume);
    s.update({10, true, false}); // No attack from held selection click.
    CHECK(!s.manualAim(enemy(9), true, true, true));
    s.update({10, false, false});
    CHECK(!s.target().valid());
    CHECK(s.leftDown({}) == ClickResult::Consume); // Duplicate down preserves owner.
    CHECK(s.leftUp() == ClickResult::Consume);
    CHECK(s.leftUp() == ClickResult::PassNative);
    CHECK(s.leftDown({}) == ClickResult::PassNative);
    s.update({10, true, true});
    CHECK(s.leftDown({PickKind::Monster, enemy()}) == ClickResult::PassNative);
    CHECK(s.leftUp() == ClickResult::PassNative); // Native pair began before mode.
    select(s);
    CHECK(s.leftDown({PickKind::NonMonster, {}}) == ClickResult::PassNative);
    CHECK(s.leftUp() == ClickResult::PassNative);
    CHECK(s.target().valid());
    CHECK(s.leftDown({}) == ClickResult::Consume); // Picker failure: no attack.
    CHECK(!s.target().valid());
    CHECK(s.leftUp() == ClickResult::Consume);
    select(s, enemy(7, 0));
    CHECK(!s.target().valid());
    select(s);
    s.update({11, true, false});
    CHECK(!s.target().valid());
    select(s);
    s.clear(); // Escape/explicit cancellation.
    CHECK(!s.target().valid());
    std::puts("click target policy PASS (not native integration)");
}
