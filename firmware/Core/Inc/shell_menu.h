/*
 * shell_menu.h
 *
 *  Created on: May 17, 2025
 *      Author: i
 */

#ifndef INC_SHELL_MENU_H_
#define INC_SHELL_MENU_H_

#include <stdint.h>
#include <stdbool.h>

#include "main.h"

typedef bool (*menu_check_for_config_change_t)();
typedef int (*printf_like_t)(const char *format, ...);

typedef enum{
	ITEM_TYPE_SELECTION,
	ITEM_TYPE_INPUT_HEX,
	ITEM_TYPE_INPUT_NUM,
	ITEM_TYPE_INPUT_CHAR,
	ITEM_TYPE_SUB,
	ITEM_TYPE_ACTION,
	ITEM_TYPE_SPACER,
	ITEM_TYPE_END
} menu_item_type_t;


typedef enum{
	MENU_STATE_NONE,
	MENU_STATE_MENU,
	MENU_STATE_INPUT_SELECTION,
	MENU_STATE_INPUT_HEX,
	MENU_STATE_INPUT_NUM,
	MENU_STATE_INPUT_CHAR
}menu_state_t;


typedef struct menu_item_t{
	char title[40];
	menu_item_type_t type;
	const char **selection_names;
	const uint32_t *selection_values;
	uint32_t selection_count;
	uint32_t setting;
	uint32_t setting_size;
	int value_size;
	bool (*action)(uint32_t* conf);
	uint8_t *value;
	struct menu_t *submenu;
	uint32_t showif_setting;
	uint32_t showif_value;
	void (*after_change)(uint32_t* conf);
	void (*current_status)(char *st);
} menu_item_t;

typedef struct menu_t{
	char title[40];
	char description[40];
	const menu_item_t *items;
	struct menu_t *parent;
	void (*onstart)(void);
	void (*onend)(void);
} menu_t;



void menu_set_print_function(printf_like_t pfunc);
void menu_init(menu_t *menu, uint32_t *config, menu_check_for_config_change_t cf);
void menu_deinit(void);
void menu_input(char in, uint32_t *config);


#endif /* INC_SHELL_MENU_H_ */
