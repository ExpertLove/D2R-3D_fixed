#include "cursor_hold.h"
#include <cstdio>
int main() {
    using namespace third_person;
    int failures=0;
    const auto check=[&](bool ok,const char* label) {
        if (!ok) { fprintf(stderr,"FAIL: %s\n",label); ++failures; }
    };
    CursorHold cursor;
    check(cursor.update(false,false)==CaptureAction::Free,"F10 off");
    check(cursor.update(true,false)==CaptureAction::Recenter,"enable without using old displacement");
    check(cursor.update(true,false)==CaptureAction::Track,"normal mouse look");
    check(cursor.update(true,true)==CaptureAction::Free,"Shift immediately suspends look");
    check(cursor.update(true,true)==CaptureAction::Free,"holding never recenters");
    check(cursor.update(true,false)==CaptureAction::Recenter,"release recenters WITHOUT rotation");
    check(cursor.update(true,false)==CaptureAction::Track,"next frame resumes look");
    cursor.update(true,true);
    check(cursor.update(false,true)==CaptureAction::Free,"Escape/Alt/F10/focus exit disables requested mode");
    check(cursor.update(false,false)==CaptureAction::Free,"Shift up cannot resurrect disabled mode");
    check(cursor.update(true,true)==CaptureAction::Free,"enable while Shift held leaves cursor free");
    check(cursor.update(true,false)==CaptureAction::Recenter,"release after enable while held");
    cursor.reset(); // OS centering failure or Shift pulse between timer ticks
    check(cursor.update(true,false)==CaptureAction::Recenter,"reset drops stale displacement");
    ShiftOwnership keys;
    check(keys.event(1,true,true) && keys.held(),"capture left Shift down");
    check(keys.event(1,true,false),"owned repeated down stays paired after exit");
    check(keys.event(2,true,true),"capture right Shift too");
    check(keys.event(1,false,false) && keys.held(),"left up consumed; right still holds cursor");
    check(keys.event(2,false,false) && !keys.held(),"last up consumed after mode exit");
    keys.initialize(1);
    check(!keys.event(1,true,true),"held before F10 never stolen on repeat");
    check(!keys.event(1,false,true) && !keys.held(),"pre-existing native Shift gets native release");
    check(!keys.event(2,true,false),"native modifier outside mode");
    check(!keys.event(2,false,false),"native paired up outside mode");
    fprintf(stdout,"cursor hold: %d failures\n",failures);
    return failures?1:0;
}
