/*
 * demo_polling.h
 *
 *  Created on: May 16, 2025
 *      Author: i
 */

#ifndef DEMO_POLLING_H_
#define DEMO_POLLING_H_

#include <stdint.h>
#include <stdbool.h>

typedef enum{
	NFC_PROC_EVT_SINGLE,
	NFC_PROC_EVT_PRE,
	NFC_PROC_EVT_POST,
} nfc_proc_evt_t;

typedef struct{
	bool event;
	uint8_t id[32];
	int idlen;
	uint8_t data[64];
	int datalen;
	char name[32];
	nfc_proc_evt_t evt_type;
} cycle_return_t;

typedef void (*tag_handle_callback_t)(cycle_return_t *ret);

void pollingDemoCycle( void );
void set_tag_handle_callback(tag_handle_callback_t f);
bool pollingDemoIni( void );

#endif /* DEMO_POLLING_H_ */
