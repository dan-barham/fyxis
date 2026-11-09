// Firmware update over Wi-Fi (Settings > Update).
//
// The board starts its own Wi-Fi network; join it from a phone or Mac, open the address shown
// and upload the new firmware (firmware/build/pyxis_bigsix.bin). It's written to the app slot
// that isn't running, and the board restarts into it. If the new firmware doesn't keep running
// for 10 s, the board goes back to the previous one on the next start.
//
// Wi-Fi is off except while an update is waiting or being received.
#pragma once
#include <stdbool.h>

typedef enum { UPD_OFF, UPD_STARTING, UPD_READY, UPD_RECEIVING, UPD_DONE, UPD_FAILED } upd_state_t;

typedef struct {
    upd_state_t state;
    int  pct;                 // while receiving
    char ssid[24];
    char pass[16];
    char url[32];
    char msg[64];             // failure reason
    char slot[20];            // the app slot running now (partition label, up to 16)
} upd_status_t;

void update_start(void);
void update_stop(void);
void update_get(upd_status_t *out);
// Call once the firmware has run long enough to trust it (cancels the automatic roll-back).
void update_mark_good(void);
