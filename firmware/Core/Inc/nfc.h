/*
 * nfc.h
 *
 *  Created on: May 15, 2025
 *      Author: i
 */

#ifndef INC_NFC_H_
#define INC_NFC_H_

#include <stdint.h>
#include <stdbool.h>

#include "demo.h"
#include "../../x-nfc/demo_polling.h"
#include "nfc_conf.h"


bool nfc_init();
void nfc_loop();

#endif /* INC_NFC_H_ */
