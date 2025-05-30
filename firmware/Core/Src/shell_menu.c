/*
 * SPDX-FileCopyrightText: 2025 Igor Brkic <igor@hyperglitch.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "shell_menu.h"
#include <stdlib.h>

menu_state_t menu_state = MENU_STATE_NONE;
menu_check_for_config_change_t check_config_change_func = NULL;
static menu_t *current_menu = NULL;
static menu_t *root;

printf_like_t shell_menu_print_func = NULL;
#define PRINTF_FUN(...)   if(shell_menu_print_func!=NULL){shell_menu_print_func(__VA_ARGS__);}

void print_item_selection(const menu_item_t *curr, uint32_t *config){
	char buff[10];
	// show list of options and wait for the input
	PRINTF_FUN("\r\n--------------------------------\r\n");
	PRINTF_FUN("SELECT %s:\r\n", curr->title);

	int sel_idx = 0;
	while(curr->selection_names[sel_idx]!=NULL){
		PRINTF_FUN("   ");
		buff[0] = (sel_idx+1) + '0';
		buff[1] = '\0';
		PRINTF_FUN("%s", buff);
		PRINTF_FUN(") ");
		PRINTF_FUN("%s", curr->selection_names[sel_idx]);
		// mark selected
		if(curr->selection_values[sel_idx]==config[curr->setting]){
			PRINTF_FUN(" (currently selected)");
		}
		PRINTF_FUN("\r\n");
		sel_idx++;
	}
	PRINTF_FUN("\r\n");
	PRINTF_FUN("Type the number in front of item to select it or q to go back\r\n");
}

void print_char_input(const menu_item_t *curr, uint32_t *config){
	PRINTF_FUN("\r\n--------------------------------\r\n");
	PRINTF_FUN("Type in the %s", curr->title);

	PRINTF_FUN(". Enter to accept new value, q to cancel.\r\n");
	PRINTF_FUN(" (allowed characters: a-z, A-Z, 0-9)\r\n");
}

void print_hex_input(const menu_item_t *curr, uint32_t *config){
	PRINTF_FUN("\r\n--------------------------------\r\n");
	PRINTF_FUN("Type in the %s", curr->title);
	uint32_t *cv = &config[curr->setting];
	uint8_t *cc = (uint8_t*)cv;

	PRINTF_FUN(". Enter to accept new value, q to cancel.\r\n");
	PRINTF_FUN(" (hex value, max length %lu bytes, current value ", curr->setting_size);
	// convert current value to printable form
	for(int i=0; i<config[curr->setting_size]; i++){
		PRINTF_FUN("%x", cc[i]);
	}
	PRINTF_FUN("):\r\n");
}


void print_num_input(const menu_item_t *curr, uint32_t *config){

	PRINTF_FUN("\r\n--------------------------------\r\n");
	PRINTF_FUN("Type in the %s", curr->title);

	PRINTF_FUN(". Enter to accept new value, q to cancel.\r\n");
	PRINTF_FUN(" (max value %d, current value %ld):\r\n", curr->value_size, config[curr->setting]);
}


void menu_print(uint32_t *config){
	PRINTF_FUN("\r\n\r\n");
	PRINTF_FUN("==================================================\r\n");
	PRINTF_FUN("%s\r\n", current_menu->title);
	PRINTF_FUN("==================================================\r\n");
	if(current_menu->description[0]!='\0'){
		PRINTF_FUN("%s\r\n", current_menu->description);
	}

	int idx = 0;
	int item_idx = 0;
	int item_real_idx = 0;
	// print menu items
	while(current_menu->items!=NULL && current_menu->items[idx].type!=ITEM_TYPE_END){
		const menu_item_t *curr = &current_menu->items[idx++];
		item_real_idx++;

		if(curr->type==ITEM_TYPE_SPACER){
			PRINTF_FUN("\r\n");
			continue;
		}
		if(curr->showif_setting!=0 && config[curr->showif_setting]!=curr->showif_value){
			continue;
		}
		item_idx++;
		PRINTF_FUN("   ");
		if(item_idx>9){
			char num[2]="a";
			num[0] = item_idx-10+'a';
			PRINTF_FUN("%s", num);
		}
		else{
			PRINTF_FUN("%d", item_idx);
		}
		PRINTF_FUN(") %s", curr->title);

		// add meta data (value, ...)
		if(curr->type==ITEM_TYPE_SELECTION){
			PRINTF_FUN(" (");
			// find current value
			const uint32_t cv = config[curr->setting];

			// scan all options
			int sz = 0;
			while(1){
				if(curr->selection_names[sz]==NULL){
					// last
					sz = -1; // error
					break;
				}
				if(curr->selection_values[sz]==cv){
					break;
				}
				sz++;
			}

			if(sz==-1){
				PRINTF_FUN("INVALID");
			}
			else{
				PRINTF_FUN("%s", curr->selection_names[sz]);
			}
			PRINTF_FUN(")");
		}
		else if(curr->type==ITEM_TYPE_INPUT_HEX){
			PRINTF_FUN(" (");
			uint32_t *c = &config[curr->setting];
			for(int i=0; i<curr->setting_size; i++){
				PRINTF_FUN("0x%x", (uint8_t)(*c));
				if(i!=curr->setting_size-1) PRINTF_FUN(" ");
				c++;
			}
			PRINTF_FUN(")");
		}
		else if(curr->type==ITEM_TYPE_INPUT_NUM){
			PRINTF_FUN(" (%ld)", config[curr->setting]);
		}
		else if(curr->type==ITEM_TYPE_INPUT_CHAR){
			PRINTF_FUN(" (");
			uint32_t *c = &config[curr->setting];
			for(int i=0; i<curr->setting_size; i++){
				if(*c==0) break;
				PRINTF_FUN("%c", (uint8_t)(*c));
				c++;
			}
			PRINTF_FUN(")");
		}
		else if(curr->type==ITEM_TYPE_ACTION && curr->current_status!=NULL){
			char stat[20] = "";
			curr->current_status(stat);
			PRINTF_FUN(" (%s)", stat);
		}
		PRINTF_FUN("\r\n");
	}

	if(current_menu->parent!=NULL){
		PRINTF_FUN("\r\n   q) Go to main menu\r\n");
	}

	if(check_config_change_func!=NULL){
		if(check_config_change_func()){
			PRINTF_FUN("\r\nNOTE: some settings are changed. Changes will be applied only after saving them.\r\n");
		}
	}
	PRINTF_FUN("\r\nType the number/letter next to the menu option, Enter to redraw menu\r\n");

}

int circ_endswith(uint8_t *buff, int idx, int bufflen, const char *search){
  // check if the circular buffer buff ends with search term
  // get search term length (must end with \0)
  int srchlen = 0;  // length without the null character
  for(int i=0; i<bufflen; i++){
    if(search[i]=='\0'){
      break;
    }
    srchlen++;
  }
  if(srchlen==0) return 0;

  for(int i=0; i<srchlen; i++){
    const int buffidx = (idx+bufflen-i)%bufflen;

    if(search[srchlen-1-i] != buff[buffidx]){
      return 0;
    }
  }
  return 1;
}

static void knm(char c){
	const uint32_t bufflen = 32;
	static uint8_t _buffer[32] = {0};
	static int buffidx = 0;
	buffidx = (buffidx+1)%bufflen;
	_buffer[buffidx] = c;
	const uint8_t the_secret_code[] = {
			0x1b, 0x5b, 0x41,
			0x1b, 0x5b, 0x41,
			0x1b, 0x5b, 0x42,
			0x1b, 0x5b,	0x42,
			0x1b, 0x5b, 0x44,
			0x1b, 0x5b, 0x43,
			0x1b, 0x5b, 0x44,
			0x1b, 0x5b, 0x43,
			0x62, 0x61,
			0x0d, 0x0a, 0x00
	};
	if(circ_endswith(_buffer, buffidx, bufflen, (const char*)the_secret_code)){
		const int l = 45;
		const uint8_t chrs[] = {
				2, 0, 1, 0, 1, 2, 0, 1, 0, 1, 2, 0, 1, 2, 0, 1, 0, 1, 0, 1, 2, 0, 1, 2, 0, 1, 0, 1, 0, 1, 2, 0,
				1, 0, 1, 0, 1, 0, 1, 2, 0, 1, 0, 1, 2
		};
		const uint8_t cln[] = {
				1, 7, 1, 9, 1, 0, 9, 1, 5, 1, 0, 7, 13, 0, 5, 3, 1, 5, 1, 3, 0, 3, 21, 0, 3, 1, 1, 13, 1, 1, 0, 3, 1, 1, 1, 9, 1, 1, 1, 0, 9, 3, 1, 3, 1
		};
		const uint32_t creds[] = {
				0x6d726946,	0x65726177,	0x20796220,	0x726f6749,	0x6b724220,
				0x0a0d6369,	0x45505948,	0x494c4752,	0x20484354,	0x0d64744c,
				0x7474680a,	0x2f3a7370,	0x7079682f,	0x6c677265,	0x68637469,
				0x6d6f632e,	0x00000a0d
		};
		char bfr[80];
		int bidx = 0;
		for(int i=0; i<l; i++){
			for(int j=0; j<=cln[i]; j++){
				switch(chrs[i]){
				case 0:
				case 1:
					bfr[bidx] = chrs[i]==0?' ':'#';
					bidx++;
					break;
				case 2:
					bfr[bidx] = '\0';
					PRINTF_FUN("%s\r\n", bfr);
					bidx = 0;
					break;
				}
			}
		}
		PRINTF_FUN("%s\r\n", (const char*)creds);
	}
}


void menu_set_print_function(printf_like_t pfunc){
	shell_menu_print_func = pfunc;
}


void menu_init(menu_t *menu, uint32_t *config, menu_check_for_config_change_t cf){
	if(current_menu!=NULL && current_menu->onend!=NULL){
		current_menu->onend();
	}
	current_menu = menu;
	root = menu;
	check_config_change_func = cf;
	menu_state = MENU_STATE_MENU;
	if(current_menu->onstart!=NULL){
		current_menu->onstart();
	}
	menu_print(config);
}


void menu_deinit(void){
	if(current_menu!=NULL && current_menu->onend!=NULL){
		current_menu->onend();
	}
	menu_state = MENU_STATE_NONE;
}


void menu_input(char in, uint32_t *config){
	static const menu_item_t * input_target = NULL;
	static char buff[20] = "";
	static int buff_idx = 0;
	knm(in);

	switch(menu_state){
	case MENU_STATE_NONE:
		// initialize and print main menu
		PRINTF_FUN("ERROR: menu not initialized\r\n");
		return;
		break;

	case MENU_STATE_MENU:
		if(in=='q'){
			if(current_menu->parent!=NULL){
				menu_init(current_menu->parent, config, NULL);
			}
		}
		else if(in=='\r'){
			// ignore
		}
		else if(in=='\n'){
			// redraw
			menu_print(config);
		}
		else{
			int idx = 0;
			int item_idx = 0;
			while(current_menu->items!=NULL && current_menu->items[idx].type!=ITEM_TYPE_END){
				const menu_item_t *curr = &current_menu->items[idx++];
				if(curr->type==ITEM_TYPE_SPACER){
					continue;
				}
				if(curr->showif_setting!=0 && config[curr->showif_setting]!=curr->showif_value){
					continue;
				}
				item_idx++;
				if((in-'0'==item_idx) || (in>='a' && in<='z' && in-'a'+10==item_idx)){
					// activate item

					switch(curr->type){
					case ITEM_TYPE_SELECTION:
						print_item_selection(curr, config);
						input_target = curr;
						menu_state = MENU_STATE_INPUT_SELECTION;
						break;
					case ITEM_TYPE_ACTION:
						if(curr->action(config)){
							menu_print(config);
						}
						break;
					case ITEM_TYPE_INPUT_HEX:
						print_hex_input(curr, config);
						input_target = curr;
						menu_state = MENU_STATE_INPUT_HEX;
						buff_idx = 0;
						break;
					case ITEM_TYPE_INPUT_NUM:
						print_num_input(curr, config);
						input_target = curr;
						menu_state = MENU_STATE_INPUT_NUM;
						buff_idx = 0;
						break;
					case ITEM_TYPE_INPUT_CHAR:
						print_char_input(curr, config);
						input_target = curr;
						menu_state = MENU_STATE_INPUT_CHAR;
						buff_idx = 0;
						break;
					case ITEM_TYPE_SUB:
						menu_init(curr->submenu, config, NULL);
						break;

					case ITEM_TYPE_SPACER:
					case ITEM_TYPE_END:
						// do nothing
						break;
					}

					break; // item activated, don't process the rest of the list
				}
			}
		}
		break;

	case MENU_STATE_INPUT_SELECTION:
		if(in-'0'>0 && in-'0'<=input_target->selection_count){
			// select
			const int sel = (in-'0') - 1;
			config[input_target->setting] = input_target->selection_values[sel];
			menu_state = MENU_STATE_MENU;
			if(input_target->after_change!=NULL){
				input_target->after_change(config);
			}
			menu_print(config);
		}
		else if(in=='q'){
			menu_state = MENU_STATE_MENU;
			menu_print(config);
		}
		else if(in=='\r'){
			// ignore
		}
		else if(in=='\n'){
			// redraw the menu
			print_item_selection(input_target, config);
			break;
		}

		break;

	case MENU_STATE_INPUT_HEX:
		if(in=='q'){
			menu_state = MENU_STATE_MENU;
			menu_print(config);
		}
		else if(in=='\n'){
			if(buff_idx==0){
				return;
			}
			// accept
			if(buff_idx%2!=0) buff_idx++;
			for(int i=0; i<buff_idx/2; i++){
				// convert to int
				config[input_target->setting+i] = ((buff[i*2]<='9')?buff[i*2]-'0':buff[i*2]-'A'+10)*16 + ((buff[i*2+1]<='9')?buff[i*2+1]-'0':buff[i*2+1]-'A'+10);
			}
			menu_state = MENU_STATE_MENU;
			if(input_target->after_change!=NULL){
				input_target->after_change(config);
			}
			menu_print(config);
		}
		else if(buff_idx>=input_target->setting_size*2){
			return;
		}
		else if((in>='0' && in<='9') || (in>='a' && in<='f') || (in>='A' && in<='F')){
			// add to payload
			if(in>='a' && in<='f'){
				// convert to uppercase
				in -= 32;
			}
			PRINTF_FUN("%c", in);
			buff[buff_idx++] = in;
		}
		break;

	case MENU_STATE_INPUT_NUM:
		if(in=='q'){
			menu_state = MENU_STATE_MENU;
			menu_print(config);
		}
		else if(in=='\n'){
			if(buff_idx==0){
				// dont' accept empty input
				return;
			}
			// accept
			buff[buff_idx] = '\0';
			uint32_t v = atoi((const char*)buff);
			if(v>input_target->value_size){
				PRINTF_FUN("\r\nInvalid input. Max value is %d, current value is %ld.\r\n", input_target->value_size, config[input_target->setting]);
				PRINTF_FUN("Please try again or press q to cancel\r\n");
				buff_idx = 0;
				return;
			}
			else{
				config[input_target->setting] = v;
				menu_state = MENU_STATE_MENU;
				if(input_target->after_change!=NULL){
					input_target->after_change(config);
				}
				menu_print(config);
			}
		}
		else if(buff_idx>=6){
			return;
		}
		else if(in>='0' && in<='9'){
			// add to payload
			PRINTF_FUN("%c", in);
			buff[buff_idx++] = in;
		}
		break;

	case MENU_STATE_INPUT_CHAR:
		if(in=='q'){
			menu_state = MENU_STATE_MENU;
			menu_print(config);
		}
		else if(in=='\n'){
			if(buff_idx==0){
				return;
			}
			// accept
			for(int i=0; i<buff_idx; i++){
				// buff_idx is definitely less than setting_size
				config[input_target->setting+i] = buff[i];
			}
			config[input_target->setting+buff_idx] = '\0';
			menu_state = MENU_STATE_MENU;
			if(input_target->after_change!=NULL){
				input_target->after_change(config);
			}
			menu_print(config);
		}
		else if(buff_idx>=input_target->setting_size){
			return;
		}
		else if((in>='0' && in<='9') || (in>='a' && in<='z') || (in>='A' && in<='Z')){
			PRINTF_FUN("%c", in);
			buff[buff_idx++] = in;
		}
		break;
	}


}
