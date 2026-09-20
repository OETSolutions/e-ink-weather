#pragma once

/* Factory reset on the board's user button (KEY1, GPIO25). See factory_reset.c for why this
 * button rather than BOOT, and why it needs a deliberate hold.
 *
 * The device has no other way to be returned to an unconfigured state: provisioning only runs
 * on a device that has no stored credentials, so without this a user who changes their WiFi
 * network (or sells the unit) has no path back except a serial console. */

/* Set up the button GPIO. Call once, early, before the first poll. Safe to call when the GPIO
 * cannot be configured — the poll then simply never reports a hold. */
void factory_reset_begin(void);

/* Check the button. Returns 1 if the hold completed (in which case the caller should stop doing
 * work and let the restart happen), 0 otherwise. Cheap: one GPIO read when not held.
 *
 * Call this at points in the boot path where abandoning the cycle is safe — between steps, not
 * in the middle of a panel write. */
int factory_reset_poll(void);

/* Block for the hold window if the button is already down. Returns 1 if the hold completed (the
 * erase has run and the device is restarting), 0 otherwise. Costs nothing when the button is up.
 *
 * This is the one to call EARLY IN BOOT: it is what makes "hold the button while powering on"
 * work, including on a device stuck in setup where nothing else ever gets to poll. */
int factory_reset_check_hold(void);

/* Erase every user-supplied setting and restart. Does not return on success. */
void factory_reset_perform(void);
