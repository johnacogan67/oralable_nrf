/*
 * One-shot Gen 2 search for the accelerometer and PPG I2C pins.
 * Gen 1 builds compile an empty stub.
 */

#ifndef I2C_PIN_SCAN_H_
#define I2C_PIN_SCAN_H_

void i2c_pin_scan_run(void);

/** Send stored scan lines on the firmware log. Call only while that notify is on. */
void i2c_pin_scan_replay(void);

#endif /* I2C_PIN_SCAN_H_ */
