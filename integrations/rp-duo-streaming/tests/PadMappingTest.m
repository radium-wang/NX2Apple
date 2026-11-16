#include <assert.h>
#include "rpduo_mac.h"

int main(void) {
    const uint16_t inputs[] = {0x2000,0x1000,0x8000,0x4000,0x40,0x80,0x100,0x200,
                               0,0,0x10,0x20,0x04,0x01,0x08,0x02};
    for (unsigned i=0; i<16; ++i) {
        if (inputs[i]) assert(rpduo_switch_buttons((rpduo_pad_frame){.buttons=inputs[i]}) == (1u<<i));
    }
    assert(rpduo_switch_buttons((rpduo_pad_frame){.buttons=0x1005,.lt=32,.rt=255}) ==
           (DM_BTN_B|DM_BTN_LEFT|DM_BTN_UP|DM_BTN_ZL|DM_BTN_ZR));
    assert(rpduo_switch_buttons((rpduo_pad_frame){.lt=31}) == 0);
    puts("Duo Switch-layout face buttons, diagonals, shoulders, triggers: PASS");
}
