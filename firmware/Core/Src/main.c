/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include "usbd_cdc_if.h"

#include "nfc.h"
#include "logger.h"
#include "instance_id.h"


#include "shell_menu.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

UART_HandleTypeDef hlpuart1;

RTC_HandleTypeDef hrtc;

TIM_HandleTypeDef htim6;

/* USER CODE BEGIN PV */

typedef enum{
	  OPMODE_CONFERENCE,
	  OPMODE_CLOCK,
	  OPMODE_NOTIFIER,
	  OPMODE_GAME,
	  OPMODE_TEST,
} operation_mode_t;

volatile operation_mode_t g_operation_mode;

// config items
typedef enum{
	SETT_HEADER = 0,
	SETT_CHECKSUM,
	SETT_MODE,
	SETT_CLOCK_RELOAD,
	SETT_CLOCK_CHIME,
	SETT_CLOCK_SHOW_SECONDS,
	SETT_BRIGHTNESS,
	SETT_SCROLL_SPEED,

	SETT_SCROLL_TEXT0,
	SETT_SCROLL_TEXT1,
	SETT_SCROLL_TEXT2,
	SETT_SCROLL_TEXT3,
	SETT_SCROLL_TEXT4,
	SETT_SCROLL_TEXT5,
	SETT_SCROLL_TEXT6,
	SETT_SCROLL_TEXT7,

	SETT_THUNT_DATA0,
	SETT_THUNT_DATA1,
	SETT_THUNT_DATA2,
	SETT_THUNT_DATA3,
	SETT_OTHERS_DATA0,
	SETT_OTHERS_DATA1,
	SETT_OTHERS_DATA2,
	SETT_OTHERS_DATA3,
	SETT_OTHERS_DATA4,
	SETT_OTHERS_DATA5,
	SETT_OTHERS_DATA6,
	SETT_OTHERS_DATA7,

	SETT_CONFIG_VERSION,
	SETT_CONFIG_END

} setting_item_t;

// default config
volatile uint32_t g_config[] = {
	0,				// SETT_HEADER
	0,				// SETT_CHECKSUM
	OPMODE_TEST,	// SETT_MODE
	3200,			// SETT_CLOCK_RELOAD
	1,				// SETT_CLOCK_CHIME
	1,				// SETT_CLOCK_SHOW_SECONDS
	20,				// SETT_BRIGHNESS
	300,			// SETT_SCROLL_SPEED

	0x53524f44,		// SETT_SCROLL_TEXT0, 	"SROD"
	0x554c4320,		// SETT_SCROLL_TEXT1, 	"ULC "
	0x30322043,		// SETT_SCROLL_TEXT2,	"02 C"
	0x00003532,		// SETT_SCROLL_TEXT3,	"..52"
	0x00000000,		// SETT_SCROLL_TEXT4
	0x00000000,		// SETT_SCROLL_TEXT5
	0x00000000,		// SETT_SCROLL_TEXT6
	0x00000000,		// SETT_SCROLL_TEXT7

	0,				// SETT_THUNT_DATA0
	0,				// SETT_THUNT_DATA1
	0,				// SETT_THUNT_DATA2
	0,				// SETT_THUNT_DATA3

	0, 				// SETT_OTHERS_DATA0
	0, 				// SETT_OTHERS_DATA1
	0, 				// SETT_OTHERS_DATA2
	0, 				// SETT_OTHERS_DATA3
	0, 				// SETT_OTHERS_DATA4
	0, 				// SETT_OTHERS_DATA5
	0, 				// SETT_OTHERS_DATA6
	0, 				// SETT_OTHERS_DATA7

	0,				// SETT_CONFIG_VERSION
	0xff			// SETT_CONFIG_END
};

volatile uint32_t g_config_loaded[sizeof(g_config)/sizeof(uint32_t)];


typedef struct{
	bool manual;
	char screen[7];
	char disp_buffer[32];
	uint32_t last_screen_update;
	int32_t txt_pos;
	int32_t txt_len;

	char disp_temp_buffer[32];
	uint32_t disp_temp_duration;
	uint32_t disp_temp_started;
	bool disp_temp_changed;
	bool disp_temp_active;
	int disp_temp_delay;
	uint8_t brightness;
} disp_text_t;

volatile disp_text_t disp_conf;


typedef struct{
	int thunt;
	int bcounter;
} quest_t;


volatile quest_t quest_data = {
		.thunt = 0,
		.bcounter = 0
};


//const uint32_t data_flash_addr = 0x8000000UL + 126*1024;
const uint32_t data_flash_addr = 0x8000000UL + 125*1024;
const uint32_t data_config_header = 0x1337;


uint32_t crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    return ~crc;
}


// -------------------------------------------------------------------
// configuration handling

bool configuration_load(){
	uint32_t address;

	// load the config and calculate sum
	address = data_flash_addr;
	for(int i=0; i<SETT_CONFIG_END; i++){
		g_config_loaded[i] = *(__IO uint32_t *)(address+4*i);
	}

	const uint32_t checksum = crc32((const uint8_t*)&g_config_loaded[SETT_MODE], (SETT_CONFIG_VERSION-SETT_MODE)*4);

	// check if valid
	if(g_config_loaded[SETT_HEADER]!=data_config_header){
		bprintf("No config found\r\n");
		return false;
	}
	if(g_config_loaded[SETT_CHECKSUM]!=checksum){
		bprintf("Config checksum mismatch\r\n");
		return false;
	}

	// copy to main config
	memcpy((uint32_t*)g_config, (uint32_t*)g_config_loaded, SETT_CONFIG_END*sizeof(uint32_t));

	bprintf("Config loaded\r\n");
	return true;
}


bool configuration_save(bool clear){
	uint32_t address;

	// update config checksum
	const uint32_t checksum = crc32((const uint8_t*)&g_config[SETT_MODE], (SETT_CONFIG_VERSION-SETT_MODE)*4);

	g_config[SETT_HEADER] = data_config_header;
	g_config[SETT_CHECKSUM] = checksum;
	g_config[SETT_CONFIG_VERSION]++;

	if(clear){
		// invalidate the header in order to reset the configuration
		g_config[SETT_HEADER] = 0;
	}

	if(HAL_FLASH_Unlock()!=HAL_OK){
		bprintf("error unlocking flash\r\n");
		return false;
	}

	// erase flash page
	__HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_OPTVERR); // Clear OPTVERR bit set on virgin samples (maybe not needed)

	FLASH_EraseInitTypeDef EraseInitStruct;
	uint32_t page_error = 0;
	EraseInitStruct.TypeErase = FLASH_TYPEERASE_PAGES;
	EraseInitStruct.PageAddress = data_flash_addr;
	EraseInitStruct.NbPages = 1;

	if(HAL_FLASHEx_Erase(&EraseInitStruct, &page_error) != HAL_OK){
		bprintf("error erasing config flash\r\n");
		HAL_FLASH_Lock();
		return false;
	}

	// write the data to the flash
	address = data_flash_addr;
	for(int i=0; i<SETT_CONFIG_END; i++){
		if(HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address, g_config[i]) == HAL_OK){
			address = address + 4;
		}
		else{
			bprintf("error saving config\r\n");
			HAL_FLASH_Lock();
			return false;
		}
	}
	bprintf("config saved\r\n");
	configuration_load();

	HAL_FLASH_Lock();
	return true;
}

int tcnt(int *tnt){
  *tnt = 0;
  uint32_t (*s)(const uint8_t *d, size_t l) = crc32;
  volatile uint32_t *p = &g_config[SETT_THUNT_DATA0];
  *tnt |= ((~((uint8_t)(((p[1]>>16)&0xff))))&0xf)!=(p[1]&0xff);
  *tnt |= s((const uint8_t*)p,1<<3)!=p[2];
  return g_config[SETT_THUNT_DATA1]&0xff;
}

bool tchk(int p){
	return g_config[SETT_THUNT_DATA0]&(1<<p);
}

void tset(int p){
	if(p<0 || p>12) return;
	int tn;
	const int cnt = tcnt(&tn);
	if(tn && cnt!=0) return;
	volatile uint32_t *t = &(g_config[SETT_THUNT_DATA0]);
	if(*t&(1<<p)) return;
	*t |= 1<<p;
	int c=0;
	for(int i=0; i<32; i++) c+= (*t)&(1<<i)?1:0;
	*(t+1) = (~((uint8_t)c))<<16 | (c);
	*(t+2) = crc32((const uint8_t*)t, 8);
	configuration_save(false);
}


bool bchk(int p){
  const int seg = p>>5;
  volatile uint32_t *d = &g_config[SETT_OTHERS_DATA0];
  return d[seg] & 1<<(p-(seg<<5));
}

int bcnt(int *tnt){
  uint32_t (*f)(const uint8_t *d, size_t l) = crc32;
  volatile uint32_t *d = &g_config[SETT_OTHERS_DATA0];
  *tnt = d[5] != f((uint8_t*)d, (1<<7)/8+4);
  return d[4];
}

void bset(int p){
  if(p<0 || p>127) return;

  int tn;
  const int cnt = bcnt(&tn);
  if(tn && cnt!=0) return;
  if(bchk(p)) return;

  uint32_t (*f)(const uint8_t *d, size_t l) = crc32;
  int seg = p>>5;
  volatile uint32_t *d = &g_config[SETT_OTHERS_DATA0];
  d[seg] |= 1<<(p-(seg<<5));
  int c=0;
  for(int dd=0; dd<4; dd++) for(int i=0; i<32; i++) c+= d[dd]&(1<<i)?1:0;
  d[4] = c;
  d[5] = f((uint8_t*)d, (1<<7)/8+4);
  configuration_save(false);
}




// -------------------------------------------------------------------
// menu actions
bool action_device_restart(uint32_t *args){
	(void)args;
	NVIC_SystemReset();
	return true;
}

bool action_save_configuration(uint32_t *args){
	(void)args;
	configuration_save(false);
	return true;
}

bool action_version_print(uint32_t *args){
	(void)args;
	bprintf("firmware version: %s\r\n", __GIT_VERSION);
	bprintf("build time: %s\r\n", __BUILD_DATE);
	bprintf("config saves: %lu\r\n", g_config[SETT_CONFIG_VERSION]);
	bprintf("instance id: %d (0x%02x)\r\n", __INSTANCE_ID, __INSTANCE_ID);
	return true;
}

bool action_factory_reset(uint32_t *args){
	configuration_save(true); // clear config
	action_device_restart(NULL); // restart
	return true;
}

void action_mode_change(uint32_t *config){

}

// -------------------------------------------------------------------
// configuration menu structure

const char *menu_modes_names[16] = {"Conference", "Clock", "Notifier", "Game", "HW Test"};
const uint32_t menu_modes_values[] = {OPMODE_CONFERENCE, OPMODE_CLOCK, OPMODE_NOTIFIER, OPMODE_GAME, OPMODE_TEST};

const char *menuyesno_names[10] = {"yes", "no"};
const uint32_t menuyesno_values[] = {1, 0};

const menu_item_t clock_menu_items[] = {
		{
				.title = "Reload value (cca 2500-3500)",
				.type = ITEM_TYPE_INPUT_NUM,
				.setting = SETT_CLOCK_RELOAD,
				.value_size = 10000,
				.showif_setting = 0,
				.after_change = NULL,
		},
		{
				.title = "Show seconds",
				.type = ITEM_TYPE_SELECTION,
				.selection_names = (const char**)menuyesno_names,
				.selection_values = menuyesno_values,
				.selection_count = 2,
				.setting = SETT_CLOCK_SHOW_SECONDS,
				.showif_setting = 0,
				.after_change = NULL,
		},
		{
				.title = "Blink on full hour (chime)",
				.type = ITEM_TYPE_SELECTION,
				.selection_names = (const char**)menuyesno_names,
				.selection_values = menuyesno_values,
				.selection_count = 2,
				.setting = SETT_CLOCK_CHIME,
				.showif_setting = 0,
				.after_change = NULL,
		},
		{
				.type = ITEM_TYPE_END,
		}
};

menu_item_t reset_menu_items[] = {
		{
				.title = "Delete game progress and text",
				.type = ITEM_TYPE_ACTION,
				.action = action_factory_reset,
				.showif_setting = 0,
				.after_change = NULL,
				.current_status = NULL,
		},
		{
				.type = ITEM_TYPE_END,
		}
};

menu_t reset_menu = {
		.title = "Factory Reset",
		.description = "DANGER: deletes game and counter data!",
		.items = reset_menu_items,
		.parent = NULL,
		.onstart = NULL,
		.onend = NULL
};

menu_item_t system_menu_items[] = {
		{
				.title = "Factory Reset",
				.type = ITEM_TYPE_SUB,
				.submenu = (struct menu_t*)&reset_menu,
				.showif_setting = 0,
				.after_change = NULL,
		},
		{
				.type = ITEM_TYPE_END,
		}
};


menu_t clock_menu = {
		.title = "Clock Settings",
		.description = "",
		.items = clock_menu_items,
		.parent = NULL,
		.onstart = NULL,
		.onend = NULL
};

menu_t system_menu = {
		.title = "System Settings",
		.description = "DANGER: deletes game and counter data!",
		.items = system_menu_items,
		.parent = NULL,
		.onstart = NULL,
		.onend = NULL
};

const menu_item_t root_items[] = {
		{
				.title = "Operation mode",
				.type = ITEM_TYPE_SELECTION,
				.selection_names = (const char**)menu_modes_names,
				.selection_values = menu_modes_values,
				.selection_count = sizeof(menu_modes_values)/sizeof(uint32_t),
				.setting = SETT_MODE,
				.showif_setting = 0,
				.after_change = action_mode_change,
		},
		{
				.type = ITEM_TYPE_SPACER
		},
		{
				.title = "LED Brightness",
				.type = ITEM_TYPE_INPUT_NUM,
				.setting = SETT_BRIGHTNESS,
				.value_size = 100,
				.showif_setting = 0,
				.after_change = NULL,
		},
		{
				.title = "Text scroll speed",
				.type = ITEM_TYPE_INPUT_NUM,
				.setting = SETT_SCROLL_SPEED,
				.value_size = 1000,
				.showif_setting = 0,
				.after_change = NULL,
		},
		{
				.type = ITEM_TYPE_SPACER
		},
		{
				.title = "Clock Settings",
				.type = ITEM_TYPE_SUB,
				.submenu = (struct menu_t*)&clock_menu,
				.showif_setting = 0,
				.after_change = NULL,
		},
		{
				.title = "System Settings",
				.type = ITEM_TYPE_SUB,
				.submenu = (struct menu_t*)&system_menu,
				.showif_setting = 0,
				.after_change = NULL,
		},


		{
				.type = ITEM_TYPE_SPACER,
		},
		{
				.title = "Print firmware version",
				.type = ITEM_TYPE_ACTION,
				.action = action_version_print,
				.showif_setting = 0,
				.after_change = NULL,
				.current_status = NULL,
		},
		{
				.title = "Restart badge",
				.type = ITEM_TYPE_ACTION,
				.action = action_device_restart,
				.showif_setting = 0,
				.after_change = NULL,
				.current_status = NULL,
		},
		{
				.title = "Save changes",
				.type = ITEM_TYPE_ACTION,
				.action = action_save_configuration,
				.showif_setting = 0,
				.after_change = NULL,
				.current_status = NULL,
		},
		{
				.type = ITEM_TYPE_END,
		}

};

menu_t root = {
		.title = "DC2025 badge configuration",
		.description = "",
		.items = root_items,
		.parent = NULL,
		.onstart = NULL,
		.onend = NULL,
};


/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_LPUART1_UART_Init(void);
static void MX_RTC_Init(void);
static void MX_TIM6_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */


// ---------------------------------------------------
// matrix handling

volatile uint8_t rx_buff[16] = {0};

const uint8_t REG_COMMAND = 0xFD;
const uint8_t REG_FRAME_0 = 0x00;
const uint8_t REG_FRAME_1 = 0x01;
const uint8_t REG_FRAME_2 = 0x02;
const uint8_t REG_FRAME_3 = 0x03;
const uint8_t REG_FRAME_4 = 0x04;
const uint8_t REG_FRAME_5 = 0x05;
const uint8_t REG_FRAME_6 = 0x06;
const uint8_t REG_FRAME_7 = 0x07;
const uint8_t REG_FRAME_8 = 0x08;
const uint8_t REG_FRAME_CONF = 0x0B;

// while picture frame selected
const uint8_t REG_PAGE_LEDS_START = 0x00;
const uint8_t REG_PAGE_LEDS_END = 0x11;
const uint8_t REG_PAGE_BLINK_START = 0x12;
const uint8_t REG_PAGE_BLINK_END = 0x23;
const uint8_t REG_PAGE_PWM_START = 0x24;
const uint8_t REG_PAGE_PWM_END = 0xB3;

// while conf page selected
const uint8_t REG_CONF = 0x00;
const uint8_t REG_PICTUREDISPLAY = 0x01;
const uint8_t REG_AUTOPLAY_CR1 = 0x02;
const uint8_t REG_AUTOPLAY_CR2 = 0x03;
const uint8_t REG_DISPOPTION = 0x05;
const uint8_t REG_AUDIOSYNC = 0x06;
const uint8_t REG_FRAMESTATE = 0x07;
const uint8_t REG_BREATH_CR1 = 0x08;
const uint8_t REG_BREATH_CR2 = 0x09;
const uint8_t REG_SHUTDOWN = 0x0A;
const uint8_t REG_AGCCR = 0x0B;
const uint8_t REG_AUDIORATE = 0x0C;

const uint8_t LED_ADDR = 0x74<<1;

uint8_t led_buffer[16] = {0};

const uint8_t char_matrix[6][9][2] = {
    {
        // char 1
        {0, 1}, // seg 0
        {0, 2}, // seg 1
        {0, 5}, // seg 2
        {0, 6}, // seg 3
        {0, 4}, // seg 4
        {0, 0}, // seg 5
        {0, 3}, // seg 6
        {2, 0}, // seg 7
        {2, 1}, // seg 8
    },
    {
        // char 2
        {2, 3}, // seg 0
        {2, 4}, // seg 1
        {4, 0}, // seg 2
        {4, 1}, // seg 3
        {2, 6}, // seg 4
        {2, 2}, // seg 5
        {2, 5}, // seg 6
        {4, 2}, // seg 7
        {4, 3}, // seg 8
    },
    {
        // char 3
        {4, 5}, // seg 0
        {4, 6}, // seg 1
        {6, 2}, // seg 2
        {6, 3}, // seg 3
        {6, 1}, // seg 4
        {4, 4}, // seg 5
        {6, 0}, // seg 6
        {6, 4}, // seg 7
        {6, 5}, // seg 8
    },
    {
        // char 4
        {8, 0}, // seg 0
        {8, 1}, // seg 1
        {8, 4}, // seg 2
        {8, 5}, // seg 3
        {8, 3}, // seg 4
        {6, 6}, // seg 5
        {8, 2}, // seg 6
        {8, 6}, // seg 7
        {10, 0}, // seg 8
    },
    {
        // char 5
        {10, 2}, // seg 0
        {10, 3}, // seg 1
        {10, 6}, // seg 2
        {12, 0}, // seg 3
        {10, 5}, // seg 4
        {10, 1}, // seg 5
        {10, 4}, // seg 6
        {12, 1}, // seg 7
        {12, 2}, // seg 8
    },
    {
        // char 6
        {12, 4}, // seg 0
        {12, 5}, // seg 1
        {14, 1}, // seg 2
        {14, 2}, // seg 3
        {14, 0}, // seg 4
        {12, 3}, // seg 5
        {12, 6}, // seg 6
        {14, 3}, // seg 7
        {14, 4}, // seg 8
    }

};

const uint32_t leds_logo_count = 39;
const uint8_t logo_matrix[39][2] = {
    {1, 0}, {1, 1}, {1, 2}, {1, 3}, {1, 4}, {1, 5},
    {3, 0}, {3, 1}, {3, 2}, {3, 3}, {3, 4}, {3, 5},
    {5, 0}, {5, 1}, {5, 2}, {5, 3}, {5, 4}, {5, 5},
    {7, 0}, {7, 1}, {7, 2}, {7, 3}, {7, 4}, {7, 5},
    {9, 0}, {9, 1}, {9, 2}, {9, 3}, {9, 4}, {9, 5},
    {11, 0}, {11, 1}, {11, 2}, {11, 3}, {11, 4}, {11, 5},
    {13, 0}, {13, 1}, {13, 2},
};

/*
 *         0
 *  	 -----
 *      |\    |
 *      | \7  |
 *    5 |  \  | 1
 *      |   \ |
 *      |  6 \|
 *       -----
 *      |\    |
 *      | \8  |
 *    4 |  \  | 2
 *      |   \ |
 *      |    \|
 *       -----
 *         3
 */
const uint8_t SEG_CHARACTERS[][9] = {
	//0  1  2  3  4  5  6  7  8
    {0, 0, 1, 0, 1, 1, 1, 1, 0}, // A
    {0, 0, 0, 1, 1, 1, 1, 1, 1}, // B
    {1, 0, 0, 1, 1, 1, 0, 0, 0}, // C
    {0, 0, 1, 1, 1, 1, 0, 1, 0}, // D
    {1, 0, 0, 1, 1, 1, 1, 0, 0}, // E
    {1, 0, 0, 0, 1, 1, 1, 0, 0}, // F
    {1, 0, 1, 1, 1, 1, 1, 0, 0}, // G
    {0, 1, 1, 0, 1, 1, 1, 0, 0}, // H
    {0, 1, 1, 0, 0, 0, 0, 0, 0}, // I
    {0, 1, 1, 1, 1, 0, 0, 0, 0}, // J
    {0, 1, 0, 0, 1, 1, 1, 0, 1}, // K
    {0, 0, 0, 1, 1, 1, 0, 0, 0}, // L
    {1, 1, 1, 0, 1, 1, 0, 1, 0}, // M
    {0, 1, 1, 0, 1, 1, 0, 0, 1}, // N
    {1, 1, 1, 1, 1, 1, 0, 0, 0}, // O
    {1, 1, 0, 0, 1, 1, 1, 0, 0}, // P
    {1, 1, 1, 1, 1, 1, 0, 0, 1}, // Q
    {0, 0, 0, 0, 1, 1, 1, 1, 1}, // R
    {1, 0, 1, 1, 0, 0, 0, 1, 0}, // S
    {1, 0, 0, 0, 1, 1, 0, 0, 0}, // T
    {0, 1, 1, 1, 1, 1, 0, 0, 0}, // U
    {0, 1, 1, 0, 0, 1, 0, 0, 1}, // V
    {0, 1, 1, 1, 1, 1, 0, 0, 1}, // W
    {0, 1, 0, 0, 1, 0, 1, 1, 1}, // X
    {0, 1, 1, 1, 0, 1, 1, 0, 0}, // Y
    {1, 1, 0, 1, 1, 0, 1, 0, 0}, // Z

	// NUMBERS (26)
    //0  1  2  3  4  5  6  7  8
	{1, 1, 1, 1, 1, 1, 0, 0, 0}, // 0
	{0, 1, 1, 0, 0, 0, 0, 0, 0}, // 1
	{1, 1, 0, 1, 1, 0, 1, 0, 0}, // 2
	{1, 1, 1, 1, 0, 0, 1, 0, 0}, // 3
	{0, 1, 1, 0, 0, 1, 1, 0, 0}, // 4
	{1, 0, 1, 1, 0, 1, 1, 0, 0}, // 5
	{1, 0, 1, 1, 1, 1, 1, 0, 0}, // 6
	{1, 1, 1, 0, 0, 0, 0, 0, 0}, // 7
	{1, 1, 1, 1, 1, 1, 1, 0, 0}, // 8
	{1, 1, 1, 1, 0, 1, 1, 0, 0}, // 9

	// EXTRA CHARACTERS (36)
    //0  1  2  3  4  5  6  7  8
	{0, 0, 0, 0, 0, 0, 0, 0, 0}, // blank
	{0, 0, 0, 0, 0, 0, 1, 0, 0}, // -
	{0, 0, 0, 1, 0, 0, 0, 0, 0}, // _
	{0, 0, 0, 1, 0, 0, 1, 0, 0}, // =
	{0, 0, 0, 0, 0, 0, 1, 1, 1}, // $
	{0, 1, 0, 0, 0, 0, 0, 0, 0}, // '
	{1, 1, 1, 1, 1, 1, 1, 1, 1}, // full, #
	{0, 0, 0, 0, 0, 0, 0, 1, 1}, // diagonals, /
	{0, 0, 0, 0, 0, 0, 0, 1, 0}, // diagonal top, backslash
	{0, 0, 0, 0, 0, 0, 0, 0, 1}, // diagonal bottom, |
	{1, 1, 0, 1, 1, 0, 0, 1, 1}, // triangles, ^
};

const char available_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_=$'#/\\|^";

#define CHAR_OFFSET_CHAR 	(0)
#define CHAR_OFFSET_NUM  	(26)
#define CHAR_OFFSET_EXTRA 	(36)

#define SEG_EXTRA_IDX_BLANK 	(0)
#define SEG_EXTRA_IDX_DASH 		(1)
#define SEG_EXTRA_IDX_UNDERLINE (2)
#define SEG_EXTRA_IDX_EQUALS	(3)
#define SEG_EXTRA_IDX_DOLLAR	(4)
#define SEG_EXTRA_IDX_QUOTE		(5)
#define SEG_EXTRA_IDX_FULL 		(6)
#define SEG_EXTRA_IDX_DIAGS   	(7)
#define SEG_EXTRA_IDX_DIAG_TOP 	(8)
#define SEG_EXTRA_IDX_DIAG_BOT 	(9)
#define SEG_EXTRA_IDX_TRIANGLES (10)

#define SEGMENT_HOR_TOP 		(1UL<<0)
#define SEGMENT_HOR_MID			(1UL<<6)
#define SEGMENT_HOR_BOT			(1UL<<3)
#define SEGMENT_VERT_L_TOP		(1UL<<5)
#define SEGMENT_VERT_L_BOT		(1UL<<4)
#define SEGMENT_VERT_R_TOP		(1UL<<1)
#define SEGMENT_VERT_R_BOT		(1UL<<2)
#define SEGMENT_DIAG_TOP		(1UL<<7)
#define SEGMENT_DIAG_BOT		(1UL<<8)


void led_select_frame(uint8_t frame){
  uint8_t d[2] = {REG_COMMAND, frame};
  HAL_I2C_Master_Transmit(&hi2c1, LED_ADDR, d, 2, 500);
}

void led_write_register(uint8_t reg, uint8_t data){
  uint8_t d[2] = {reg, data};
  HAL_I2C_Master_Transmit(&hi2c1, LED_ADDR, d, 2, 500);
}

uint8_t led_read_register(uint8_t reg){
	uint8_t d;
	HAL_I2C_Master_Receive(&hi2c1, LED_ADDR, &d, 1, 100);
	return d;
}

void matrix_write(const uint8_t *rowcolumn, uint8_t value){
  const uint8_t row = rowcolumn[0];
  const uint8_t column = rowcolumn[1];
  if(value)
    led_buffer[row] |= 1<<column;
  else
    led_buffer[row] &= ~(1<<column);
}

void matrix_update(){
  for(int i=0; i<16; i++){
    led_write_register(REG_PAGE_LEDS_START+i, led_buffer[i]);
  }
}

void matrix_clear(){
  for(int i=REG_PAGE_LEDS_START; i<REG_PAGE_LEDS_END; i+=1){
    led_write_register(i, 0x00);
  }
  for(int i=0; i<16; i++){
    led_buffer[i] = 0;
  }
}


uint8_t matrix_get_pwm_a(){
	return led_read_register(REG_PAGE_PWM_START);
}


void matrix_pwm_all(uint8_t bank_a, uint8_t bank_b){
	led_select_frame(REG_FRAME_0);
	uint8_t payload_full[145] = {0};
	payload_full[0] = REG_PAGE_PWM_START;
	uint8_t *payload = &(payload_full[1]);
	for(int i=0; i<144; i+=16){
		for(int j=0; j<8; j++){
			payload[i+j] = bank_a;
		}
		for(int j=0; j<8; j++){
			payload[i+8+j] = bank_b;
		}
	}
	HAL_I2C_Master_Transmit(&hi2c1, LED_ADDR, payload_full, 144, 400);
}


void matrix_write_char(int pos, char c){
  uint8_t *p;
  if(c>='0' && c<='9'){
    p = (uint8_t*)SEG_CHARACTERS[CHAR_OFFSET_NUM + c-'0'];
  }
  else if(c>='a' && c<='z'){
    p = (uint8_t*)SEG_CHARACTERS[CHAR_OFFSET_CHAR + c-'a'];
  }
  else if(c>='A' && c<='Z'){
    p = (uint8_t*)SEG_CHARACTERS[CHAR_OFFSET_CHAR + c-'A'];
  }
  else if(c=='#'){
	  p = (uint8_t*)SEG_CHARACTERS[CHAR_OFFSET_EXTRA + SEG_EXTRA_IDX_FULL];
  }
  else if(c=='\\'){
	  p = (uint8_t*)SEG_CHARACTERS[CHAR_OFFSET_EXTRA + SEG_EXTRA_IDX_DIAG_BOT];
  }
  else if(c=='/'){
	  p = (uint8_t*)SEG_CHARACTERS[CHAR_OFFSET_EXTRA + SEG_EXTRA_IDX_DIAG_TOP];
  }
  else if(c=='|'){
	  p = (uint8_t*)SEG_CHARACTERS[CHAR_OFFSET_EXTRA + SEG_EXTRA_IDX_DIAGS];
  }
  else if(c=='^'){
	  p = (uint8_t*)SEG_CHARACTERS[CHAR_OFFSET_EXTRA + SEG_EXTRA_IDX_TRIANGLES];
  }
  else if(c=='_'){
	  p = (uint8_t*)SEG_CHARACTERS[CHAR_OFFSET_EXTRA + SEG_EXTRA_IDX_UNDERLINE];
  }
  else if(c=='-'){
	  p = (uint8_t*)SEG_CHARACTERS[CHAR_OFFSET_EXTRA + SEG_EXTRA_IDX_DASH];
  }
  else if(c=='='){
	  p = (uint8_t*)SEG_CHARACTERS[CHAR_OFFSET_EXTRA + SEG_EXTRA_IDX_EQUALS];
  }
  else if(c=='$'){
	  p = (uint8_t*)SEG_CHARACTERS[CHAR_OFFSET_EXTRA + SEG_EXTRA_IDX_DOLLAR];
  }
  else if(c=='\''){
	  p = (uint8_t*)SEG_CHARACTERS[CHAR_OFFSET_EXTRA + SEG_EXTRA_IDX_QUOTE];
  }
  else{
    p = (uint8_t*)&SEG_CHARACTERS[CHAR_OFFSET_EXTRA + SEG_EXTRA_IDX_BLANK];
  }
  for(int i=0; i<9; i++){
    const uint8_t *rc = char_matrix[pos][i];
    matrix_write(rc, p[i]);
  }
}

void matrix_write_custom_char(int pos, uint16_t segments){
  for(int i=0; i<9; i++){
	const uint8_t *rc = char_matrix[pos][i];
	matrix_write(rc, (segments&(1UL<<i)) ? 1 : 0);
  }
}

void set_screen(const char *c){
  for(int i=0; i<6; i++){
    matrix_write_char(i, c[i]);
  }
  matrix_update();
}

void screen_shift(char *c, int offset){
  const int len = strlen(c);
  if(offset<0){
    for(int i=0; i<len; i++){
      if(i >= (len-offset)){
        c[i] = ' ';
      }
      else{
        c[i] = c[i-offset];
      }
    }
  }
  else{

  }
}

volatile bool rtc_update_flag = false;
volatile uint8_t tm_seconds = 0;
volatile uint8_t tm_minutes = 37;
volatile uint8_t tm_hours = 13;

void HAL_RTCEx_WakeUpTimerEventCallback(RTC_HandleTypeDef *hrtc){
	tm_seconds++;
	rtc_update_flag = true;
}

volatile uint32_t button_pressed_tm[2] = {0, 0};
volatile bool button_pressed[2] = {false, false};
volatile bool button_clicked[2] = {false, false};
volatile bool button_long_press[2] = {false, false};

void HAL_GPIO_EXTI_Callback(uint16_t pin){
	int idx = -1;
	int state = -1;

	if(pin==BUTTON0_Pin){
		idx = 0;
		state = HAL_GPIO_ReadPin(BUTTON0_GPIO_Port, BUTTON0_Pin);
		bprintf("#BTN::0::%s$\r\n", state==0 ? "press" : "release");
	}
	else if(pin==BUTTON1_Pin){
		idx = 1;
		state = HAL_GPIO_ReadPin(BUTTON1_GPIO_Port, BUTTON1_Pin);
		bprintf("#BTN::1::%s$\r\n", state==0 ? "press" : "release");
	}
	else if(pin==BUTTON1ALT_Pin){
		idx = 1;
		state = HAL_GPIO_ReadPin(BUTTON1ALT_GPIO_Port, BUTTON1ALT_Pin);
		bprintf("#BTN::1::%s$\r\n", state==0 ? "press" : "release");
	}
	else{
		return;
	}

	if(state==0){
		button_pressed[idx] = true;
		button_pressed_tm[idx] = HAL_GetTick();
	}
	else{
		const uint32_t tm = HAL_GetTick();
		button_pressed[idx] = false;
		if(tm-button_pressed_tm[idx]<300){
			button_clicked[idx] = true;
			bprintf("#BTN::%d::click$\r\n", idx);
		}
		else if(tm-button_pressed_tm[idx]>1000){
			button_long_press[idx] = true;
			bprintf("#BTN::%d::longpress$\r\n", idx);
		}
	}
}



void manual_set_text(char *disp, int len){
	uint32_t blink_last = 0;
	uint32_t refresh_last = 0;
	int blink_state = 0;
	const uint32_t blink_rate = 300;
	int idx = 0;
	const int screen_size = 6;
	uint32_t last_auto_inc = 0;
	bool refresh = false;

	char screen[7] = "CONF  ";

	button_clicked[0] = false;
	button_clicked[1] = false;

	// fill the rest of the buffer with zeros
	for(int i=strnlen(disp, len); i<len; i++){
		disp[i] = '\0';
	}

	set_screen(screen);
	// wait for the button release
	while(button_pressed[0]){
		HAL_Delay(100);
	}

	while(true){
		const uint32_t tm = HAL_GetTick();

		// inactivity timeout
		if(!button_pressed[0] && !button_pressed[1] && tm-button_pressed_tm[0]>20000 && tm-button_pressed_tm[1]>20000){
			set_screen("TMOUT ");
			HAL_Delay(1000);
			return;
		}

		// left click - move position
		if(button_clicked[0]){
			button_clicked[0] = false;
			idx++;
			if(idx>=len) idx = 0;
			blink_state = 0;
			blink_last = 0;
			refresh = true;
		}

		// right click - change character
		if(button_clicked[1] || (button_pressed[1] && tm-button_pressed_tm[1]>500 && tm-last_auto_inc>100)){
			if(button_clicked[1]){
				button_clicked[1] = false;
			}
			else{
				last_auto_inc = tm;
			}

			// find current character in the available char list
			int avidx = -1;
			for(int i=0; i<sizeof(available_chars); i++){
				if(disp[idx]==available_chars[i]){
					avidx = i;
				}
			}
			if(avidx==-1) avidx = 0; // not in the list
			avidx++; // increment
			if(avidx>=sizeof(available_chars)) avidx = 0; // wrap
			disp[idx] = available_chars[avidx]; // set
			blink_state = 1; // show char
			refresh = true;
		}

		if(tm-refresh_last>100){
			refresh = true;
		}

		// config done, save
		if(button_pressed[0] && tm-button_pressed_tm[0]>3000){
			memcpy((uint32_t*)&g_config[SETT_SCROLL_TEXT0], disp, 32);
			configuration_save(false);
			return;
		}

		if(!refresh) continue;

		// update screen
		if(idx<screen_size){
			// first part of the buffer, pointer can be at any of the first 6 characters
			memcpy(screen, disp, 6);
			if(blink_state==0){
				if(screen[idx]==' ') screen[idx] = '_';
				else screen[idx] = ' ';
			}
		}
		else{
			// pointer is at the last screen character
			memcpy(screen, disp+(idx-5), 6);
			if(blink_state==0){
				if(screen[5]==' ') screen[5] = '_';
				else screen[5] = ' ';
			}
		}

		if(tm-blink_last>blink_rate && !button_pressed[1]){
			blink_last = tm;
			blink_state ^= 0x01;
		}

		set_screen(screen);
	}
}

int buffer_text_length(const char *buff, int len){
	// find last non blank character
	for(int i=len-1; i>=0; i--){
		if(buff[i]!=' ' && buff[i]!=0) return i+1;
	}
	return 0;
}


void init_display_text(const char *init_text){
	disp_conf.manual = false;
	memset((uint8_t*)disp_conf.disp_buffer, 0, sizeof(disp_conf.disp_buffer)); // clear the buffer
	strncpy((char*)disp_conf.screen, "      ", sizeof(disp_conf.screen));
	strncpy((char*)disp_conf.disp_buffer, init_text, sizeof(disp_conf.disp_buffer));
	disp_conf.last_screen_update = 0;
	disp_conf.txt_pos = 0;
	disp_conf.txt_len = buffer_text_length((const char*)disp_conf.disp_buffer, sizeof(disp_conf.disp_buffer));
	disp_conf.disp_temp_delay = 0;
}

void display_text_set(const char* text){
	if(text!=NULL){
		strncpy((char*)disp_conf.disp_buffer, text, sizeof(disp_conf.disp_buffer));
	}
	disp_conf.txt_pos = 0;
	disp_conf.txt_len = buffer_text_length((const char*)disp_conf.disp_buffer, sizeof(disp_conf.disp_buffer));
	disp_conf.last_screen_update = 0;
}

void display_text_set_temp(const char* text, uint32_t duration){
	if(text==NULL) return;
	strncpy((char*)disp_conf.disp_temp_buffer, text, sizeof(disp_conf.disp_buffer));
	disp_conf.disp_temp_duration = duration;
	disp_conf.disp_temp_changed = true;
}


char disp_tmp[32];
void display_text_update(disp_text_t *conf){
	const int screen_size = 6;
	const uint32_t tm = HAL_GetTick();

	// set brightness if needed
	if(conf->brightness!=g_config[SETT_BRIGHTNESS]){
		conf->brightness = g_config[SETT_BRIGHTNESS];
		//bprintf("bset\r\n");
		const uint8_t b = g_config[SETT_BRIGHTNESS];
		matrix_pwm_all(b, b>4?b/4:1);
		return;
	}

	if(conf->manual){
		return;  // display is controlled from external source
	}

	// check if temporary text is set
	if(conf->disp_temp_changed){
		if(!conf->disp_temp_active){
			// save current buffer (don't do it in case it's temporary)
			memcpy(disp_tmp, conf->disp_buffer, sizeof(disp_tmp));
		}
		display_text_set(conf->disp_temp_buffer);
		conf->disp_temp_active = true;
		conf->disp_temp_started = tm;
		conf->disp_temp_changed = false;
		conf->disp_temp_delay = 5;
	}

	if(conf->disp_temp_active && tm-conf->disp_temp_started > conf->disp_temp_duration){
		// switch back to main text
		display_text_set(disp_tmp);
		conf->disp_temp_active = false;
		conf->disp_temp_delay = 3;
	}

	if(conf->disp_temp_delay>0){
		conf->disp_temp_delay--;
		set_screen("      ");
		return;
	}

	if(tm-conf->last_screen_update > g_config[SETT_SCROLL_SPEED]){
		conf->last_screen_update = tm;

		if(conf->txt_len>screen_size){
			memcpy(conf->screen, "      ", screen_size);
			if(conf->txt_pos>conf->txt_len){
				// roll in the string again
				int bpos = 0;
				for(int i=screen_size-(conf->txt_pos-conf->txt_len); i<screen_size; i++){
					conf->screen[i] = conf->disp_buffer[bpos++];
				}
			}
			else{
				// just scroll it
				for(int i=0; i<screen_size; i++){
					if(conf->txt_pos+i>=conf->txt_len){
						// end of string, go until the characters leave the screen
						conf->screen[i] = ' ';
					}
					else{
						conf->screen[i] = conf->disp_buffer[conf->txt_pos+i];
					}
				}
			}
			conf->txt_pos++;
			if(conf->txt_pos>conf->txt_len+screen_size-1) conf->txt_pos = 0;
		}
		else{
			// just print it
			strncpy(conf->screen, "      ", sizeof(conf->screen));
			strncpy(conf->screen, conf->disp_buffer, sizeof(conf->screen));
		}
		set_screen(conf->screen);
	}
}

void display_text_set_external(bool en){
	disp_conf.manual = en;
}

volatile bool got_tag = false;
volatile bool thunt_update = false;
volatile bool thunt_new = false;
volatile int thunt_last = -1;
volatile bool counter_update = false;
volatile int counter_last = -1;

void badge_handle_tag(cycle_return_t *t){
	if(!t->event) return;

	cycle_return_t tag;
	memcpy(&tag, t, sizeof(cycle_return_t));

	if(tag.evt_type==NFC_PROC_EVT_SINGLE || tag.evt_type==NFC_PROC_EVT_PRE){
		got_tag = true;
	}
	bprintf("#NFC::%s::%s::%s$\r\n", tag.name, (char*)hex2Str(tag.id, tag.idlen), (char*)hex2Str(tag.data, tag.datalen));

	const uint8_t data[] = {
		 0x00, 0x1F, 0x01, 0xEF, 0x01, 0xCF, 0x01, 0xFF, 0x00, 0x0F, 0x01, 0xBF, 0x00, 0x7F,
		 0x00, 0x6F, 0x00, 0x5F, 0x00, 0x2F, 0x00, 0x4F, 0x00, 0x3F,
	};
	if(tag.idlen==4){
		for(int i=0; i<sizeof(data); i+=2){
			if(tag.id[0]+3==data[i+1] && tag.id[1]==0x33-data[i]){
				if((tag.id[2]^0x2a)==0x45 && ((tag.id[3]^0x83)+24)==0x9a){
					thunt_last = i/2;
					thunt_new = !tchk(i/2);
					thunt_update = true;
					tset(i/2);
					break;
				}
			}
		}

	    if(tag.id[0]==0x5F && tag.id[1]==0x48){
	    	if(tag.id[3]==(0x8f^tag.id[2])-1){
	    		counter_update = true;
	    		counter_last = tag.id[2];
	    		bset(tag.id[2]);
	    	}
	    }

	}
	if(tag.idlen==8){
		if(tag.id[0] == 0x02 && tag.id[1]==0xFE){
			const uint8_t k[] = {0x23, 0x2c, 0xba, 0xfe};
			if((tag.id[2]^k[0]) == (tag.id[3]^k[1])){;
				if(tag.id[4] == 0x44 && (tag.id[5]^k[3])==0xbd){
					if((0x8f^tag.id[6])-1 == tag.id[7]){
						counter_update = true;
						counter_last = tag.id[6];
						bset(tag.id[6]);
					}
				}
			}
		}
	}
}


void handle_conference(){
	// load the screen text from settings
	char text[32];
	memcpy(text, (uint8_t*)&g_config[SETT_SCROLL_TEXT0], 32);

	init_display_text(text);
	set_tag_handle_callback(badge_handle_tag);

	button_clicked[0] = false;
	button_clicked[1] = false;

	uint32_t disp_state_changed = 0;
	const uint32_t disp_delay = 3500;

	//uint8_t led_state[5] = {0}; // 39 logo leds

	while(true){
		const uint32_t tm = HAL_GetTick();

		if(got_tag){
			got_tag = false;
			display_text_set_external(true);
			set_screen("      ");
			for(int char_idx=0; char_idx<6; char_idx++){
				matrix_write_custom_char(char_idx, SEGMENT_VERT_L_TOP | SEGMENT_VERT_L_BOT);
				matrix_update();
				HAL_Delay(100);
				matrix_write_custom_char(char_idx, SEGMENT_VERT_L_TOP | SEGMENT_VERT_L_BOT | SEGMENT_VERT_R_TOP | SEGMENT_VERT_R_BOT);
				matrix_update();
				HAL_Delay(100);
			}
			display_text_set_external(false);
		}
		if(thunt_update){
			thunt_update = false;
			char msg[32];
			snprintf(msg, sizeof(msg), "   TREASURE FOUND %d", thunt_last+1);
			display_text_set_temp(msg, 7000);
		}
		if(counter_update){
			counter_update = false;
			char msg[32];
			snprintf(msg, sizeof(msg), "   NEW BADGE %d", counter_last);
			display_text_set_temp(msg, 7000);
		}

		if(button_clicked[1]){
			button_clicked[1] = false;

			if(tm-disp_state_changed>disp_delay){
				// show badge counter
				disp_state_changed = tm;
				char msg[32];
				int tn;
				const int cnt = bcnt(&tn);
				snprintf(msg, sizeof(msg), "BADGES %d %c", cnt, tn?'\'':' ');
				display_text_set_temp(msg, disp_delay);
			}
			else{
				// show treasure hunt status
				disp_state_changed = 0;
				char msg[32];
				int tn;
				const int cnt = tcnt(&tn);
				snprintf(msg, sizeof(msg), "TREASURE %d %c", cnt, tn?'\'':' ');
				display_text_set_temp(msg, disp_delay);
			}
		}

		if(button_pressed[0] && tm-button_pressed_tm[0]>3000){
			display_text_set_external(true);
			manual_set_text((char*)disp_conf.disp_buffer, sizeof(disp_conf.disp_buffer));
			display_text_set(NULL);
			set_screen("OK    ");

			// wait for the button release
			while(button_pressed[0]){
				HAL_Delay(100);
			}
			display_text_set_external(false);
		}

		int t;
		const int ld[] = {5,4,4,5,4,4,4,5,4};
		int l = 0;
		for(int i=0; i<tcnt(&t); i++){
			l+=ld[i];
		}
		for(int i=0; i<39; i++){
			matrix_write(logo_matrix[i], i<l);
		}

		nfc_loop();
	}
}


void manual_set_clock(uint8_t *hour, uint8_t *minute, uint8_t *second){
	uint32_t blink_last = 0;
	uint32_t refresh_last = 0;
	int blink_state = 0;
	const uint32_t blink_rate = 300;
	int idx = 0;
	uint32_t last_auto_inc = 0;
	bool refresh = false;

	char screen[7] = "CONF  ";
	char disp[15] = "xxxxxx";
	const int len = 6;

	button_clicked[0] = false;
	button_clicked[1] = false;

	set_screen(screen);
	// wait for the button release
	while(button_pressed[0]){
		HAL_Delay(100);
	}

	snprintf(disp, sizeof(disp), "%02d%02d%02d", *hour, *minute, *second);

	while(true){
		const uint32_t tm = HAL_GetTick();

		// inactivity timeout
		if(!button_pressed[0] && !button_pressed[1] && tm-button_pressed_tm[0]>20000 && tm-button_pressed_tm[1]>20000){
			set_screen("TMOUT ");
			HAL_Delay(1000);
			return;
		}

		// left click - move position
		if(button_clicked[0]){
			button_clicked[0] = false;
			idx+=2;
			if(idx>len-2) idx = 0;
			blink_state = 0;
			blink_last = 0;
			refresh = true;
		}

		// right click - change character
		if(button_clicked[1] || (button_pressed[1] && tm-button_pressed_tm[1]>500 && tm-last_auto_inc>100)){
			if(button_clicked[1]){
				button_clicked[1] = false;
			}
			else{
				last_auto_inc = tm;
			}

			// find the number and increment it
			switch(idx){
			case 0:
				(*hour)++;
				if(*hour>23){
					*hour = 0;
				}
				break;
			case 2:
				(*minute)++;
				if(*minute>59){
					*minute = 0;
				}
				break;
			case 4:
				(*second)++;
				if(*second>59){
					*second = 0;
				}
				break;
			}
			snprintf(disp, sizeof(disp), "%02d%02d%02d", *hour, *minute, *second);
			blink_state = 1; // show char
			refresh = true;
		}

		if(tm-refresh_last>100){
			refresh = true;
		}

		// config done
		if(button_pressed[0] && tm-button_pressed_tm[0]>3000){
			return;
		}

		if(!refresh) continue;

		// update screen
		memcpy(screen, disp, 6);
		if(blink_state==0){
			screen[idx] = ' ';
			screen[idx+1] = ' ';
		}

		if(tm-blink_last>blink_rate && !button_pressed[1]){
			blink_last = tm;
			blink_state ^= 0x01;
		}

		set_screen(screen);
	}
}


void handle_clock(){
	// enable the RTC interrupt
	// LSI is 37kHz nominally but can be anything in the 30-60kHz range
	// interrupt will trigger every <reload> fRTC/16 cycles
	// in case of 37khz LSI, fRTC/16=2.3125kHz
	// reload = fRTC/16-1

	init_display_text("");
	display_text_set_external(true);

	HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, g_config[SETT_CLOCK_RELOAD], RTC_WAKEUPCLOCK_RTCCLK_DIV16); // generate interrupt every second, 2299 "theoretical" value
	tm_seconds = 1; // reset seconds

	while(true){
		const uint32_t tm = HAL_GetTick();

		if(button_pressed[0] && tm-button_pressed_tm[0]>3000){
			manual_set_clock((uint8_t*)&tm_hours, (uint8_t*)&tm_minutes, (uint8_t*)&tm_seconds);
			display_text_set(NULL);
			set_screen("OK    ");

			// wait for the button release
			while(button_pressed[0]){
				HAL_Delay(100);
			}
		}


		if(rtc_update_flag){
			rtc_update_flag = false;

			// RTC is running on LSI which is wildly inaccurate so we can't use RTC as a time source.
			// We're using RTC interrupt for "ticking" while keeping the time in software
			if(tm_seconds>=60){
				tm_seconds = 0;
				tm_minutes++;
				if(tm_minutes>=60){
					tm_minutes = 0;
					tm_hours++;
					if(tm_hours>=24){
						tm_hours = 0;
					}
				}
			}

			char disp[10];
			if(g_config[SETT_CLOCK_SHOW_SECONDS]){
				snprintf(disp, sizeof(disp), "%02u%02u%02u", tm_hours, tm_minutes, tm_seconds);
			}
			else{
				snprintf(disp, sizeof(disp), " %02u%02u ", tm_hours, tm_minutes);
			}

			// "chime"
			if(g_config[SETT_CLOCK_CHIME]){
				int logo_val = 0;
				if(tm_minutes==0 && tm_seconds==0) logo_val = 1;
				else if(tm_hours==13 && tm_minutes==37 && tm_seconds==0){
					strncpy(disp, " 1337 ", sizeof(disp));
					logo_val = 1;
				}
				for(int i=0; i<39; i++) matrix_write(logo_matrix[i], logo_val);
			}

			set_screen(disp);
			matrix_update();
		}
		HAL_Delay(20);
	}
}


typedef enum{
	NOTIFIER_MODE_TEXT,
	NOTIFIER_MODE_CUSTOM,
} notifier_mode_t;

volatile notifier_mode_t notifier_mode = NOTIFIER_MODE_TEXT;
volatile uint8_t notifier_leds[39] = {0};
volatile bool notifier_leds_changed = true;
volatile char notifier_string[32] = "";
volatile uint8_t notifier_segments[6][9] = {0};
volatile bool notifier_update = false;

void handle_notifier(){
	init_display_text("CTRL");
	set_tag_handle_callback(badge_handle_tag);

	notifier_mode_t old_mode = notifier_mode;
	while(true){
		switch(notifier_mode){
		case NOTIFIER_MODE_TEXT:
			if(old_mode==NOTIFIER_MODE_CUSTOM || notifier_update){
				notifier_update = false;
				old_mode = notifier_mode;
				// reset text
				display_text_set((const char*)notifier_string);
				display_text_set_external(false);
			}
			break;
		case NOTIFIER_MODE_CUSTOM:
			if(old_mode==NOTIFIER_MODE_TEXT){
				old_mode = notifier_mode;
				display_text_set_external(true);
			}
			for(int pos_idx=0; pos_idx<6; pos_idx++){
				for(int seg_idx=0; seg_idx<9; seg_idx++){
				    const uint8_t *rc = char_matrix[pos_idx][seg_idx];
				    matrix_write(rc, notifier_segments[pos_idx][seg_idx]);
				}
			}
			break;
		}

		// handle leds
		if(notifier_leds_changed){
			notifier_leds_changed = false;
			for(int i=0; i<39; i++){
				matrix_write(logo_matrix[i], notifier_leds[i]);
			}
			matrix_update();
		}

		nfc_loop();
	}
}


bool handle_notifier_command(uint8_t *buffer){
	// expects a null terminated string

	bool clear_text = false;
	bool clear_leds = false;

	switch(buffer[0]){
	case 's':
	case 'S':
		// string command, set current text
		strncpy((char*)notifier_string, (char*)buffer+1, sizeof(notifier_string));
		notifier_mode = NOTIFIER_MODE_TEXT;
		notifier_update = true;
		return true;
		break;
	case 'c':
	case 'C':
		// clear command, clear text and/or leds
		if(buffer[1]=='t' || buffer[1]=='T'){
			clear_text = true;
		}
		if(buffer[1]=='l' || buffer[1]=='L'){
			clear_leds = true;
		}
		else{
			clear_text = true;
			clear_leds = true;
		}
		if(clear_text){
			notifier_mode = NOTIFIER_MODE_CUSTOM;
			for(int pos_idx=0; pos_idx<6; pos_idx++){
				for(int seg_idx=0; seg_idx<9; seg_idx++){
					notifier_segments[pos_idx][seg_idx] = 0;
				}
			}
		}
		if(clear_leds){
			for(int i=0; i<39; i++){
				notifier_leds[i] = 0;
			}
			notifier_leds_changed = true;
		}
		notifier_update = true;
		return true;
		break;
	case 'g':
	case 'G':
		// control one segment of the display
		// G<char_idx><seg_idx><1|0>
		if(buffer[1]<'0' || buffer[1]>'5' || buffer[2]<'0' || buffer[2]>'8' || buffer[3]<'0' || buffer[3]>'1'){
			return false;
		}
		else{
			notifier_mode = NOTIFIER_MODE_CUSTOM;
			const int pos = buffer[1]-'0';
			const int seg = buffer[2]-'0';
			notifier_segments[pos][seg] = buffer[3]-'0';
			notifier_leds_changed = true;
			notifier_update = true;
			return true;
		}
		break;
	case 'l':
	case 'L':
		// control one led
		// LXX<1|0>
		if(buffer[1]<'0' || buffer[1]>'3' || buffer[2]<'0' || buffer[2]>'9' || buffer[3]<'0' || buffer[3]>'1'){
			return false;
		}
		const int led_idx = (buffer[1]-'0')*10 + (buffer[2]-'0');
		notifier_leds[led_idx] = buffer[3]-'0';
		notifier_leds_changed = true;
		return true;
		break;
	case 'b':
	case 'B':
		// Bxxx, 000-100
		if(buffer[1]<'0' || buffer[1]>'9' || buffer[2]<'0' || buffer[2]>'9'){
			return false;
		}
		const int b = (buffer[1]-'0')*10 + (buffer[2]-'0');
		g_config[SETT_BRIGHTNESS] = b;
		return true;
		break;
	}
	return false;
}



volatile uint8_t comm_buffer[32] = {0};
volatile uint32_t comm_buffer_idx = 0;
void handle_serial_char(char c){
	if(g_operation_mode==OPMODE_NOTIFIER){
		if(c=='\r') return; // ignore
		if(c=='\n'){
			// handle buffer
			comm_buffer[comm_buffer_idx] = '\0';
			handle_notifier_command((uint8_t*)comm_buffer);
			comm_buffer_idx = 0;
		}
		else{
			comm_buffer[comm_buffer_idx] = c;
			comm_buffer_idx++;
			if(comm_buffer_idx>=sizeof(comm_buffer)){
				comm_buffer_idx = sizeof(comm_buffer)-1; // prevent overflow
			}
		}
	}
	else{
		menu_input(c, (uint32_t*)g_config);
	}
}

void usb_cdc_rx_func(uint8_t *data, uint32_t len){
	//CDC_Transmit_FS(data, len); // local echo
	for(int i=0; i<len; i++){
		handle_serial_char(data[i]);
	}
}

void uart_rx_func(uint8_t *data, uint32_t len){
	//(void)HAL_UART_Transmit(&hlpuart1, uart_input_buffer, 1, 1);
	for(int i=0; i<len; i++){
		handle_serial_char((char)data[i]);
	}
}


volatile bool hwtest_tag_found;
volatile uint8_t hwtest_tag_uid[32];
volatile int hwtest_tag_uid_len = 0;

void hwtest_handle_tag(cycle_return_t *ret){
	if(!ret->event) return;

	char msg[32];
	sprintf(msg, "%s", (char*)hex2Str(ret->id, ret->idlen));

	if(ret->idlen>0){
		printf("uid: ");
		for(int i=0; i<ret->idlen; i++){
			printf("%02x ", ret->id[i]);
			hwtest_tag_uid[i] = ret->id[i];
		}
		hwtest_tag_uid_len = ret->idlen;
		hwtest_tag_found = true;
		printf("\r\n");
	}
	if(ret->datalen>0){
		printf("data: ");
		for(int i=0; i<ret->datalen; i++){
			printf("%02x ", ret->data[i]);
		}
		printf("\r\n");
	}

}


void handle_hwtest(){
	init_display_text(" TEST");
	printf("TEST\r\n");
	HAL_Delay(1000);
	display_text_set("      ");
	HAL_Delay(300);


	printf("BTN1\r\n");
	display_text_set("BTN 1");
	button_clicked[0] = false;
	while(1){
		if(button_clicked[0]){
			button_clicked[0] = false;
			break;
		}
	}

	display_text_set("      ");
	HAL_Delay(300);
	display_text_set("BTN 2");
	printf("BTN2\r\n");
	button_clicked[1] = false;
	while(1){
		if(button_clicked[1]){
			button_clicked[1] = false;
			break;
		}
	}
	display_text_set("      ");
	HAL_Delay(300);
	display_text_set("######");
	printf("######\r\n");
	while(1){
		if(button_clicked[0] || button_clicked[1]){
			button_clicked[0] = false;
			button_clicked[1] = false;
			break;
		}
	}
	display_text_set("      ");
	for(int i=0; i<39; i++){
		matrix_write(logo_matrix[i], 1);
	}
	matrix_update();
	printf("logo led test\r\n");
	while(1){
		if(button_clicked[0] || button_clicked[1]){
			button_clicked[0] = false;
			button_clicked[1] = false;
			break;
		}
	}
	matrix_clear();

	set_tag_handle_callback(hwtest_handle_tag);
	display_text_set("NFC");
	printf("NFC\r\n");
	while(1){
		nfc_loop();
		if(hwtest_tag_found){
			char msg[32];
			snprintf(msg, sizeof(msg), "%s", (char*)hex2Str((uint8_t*)hwtest_tag_uid, hwtest_tag_uid_len));
			display_text_set(msg);
			break;
		}
	}
	HAL_Delay(1500);

	display_text_set("OK");
	printf("OK?\r\n");
	while(1){
		if(button_clicked[0] || button_clicked[1]){
			button_clicked[0] = false;
			button_clicked[1] = false;
			break;
		}
	}

	// switch to conf mode and save
	g_config[SETT_MODE] = OPMODE_CONFERENCE;
	configuration_save(false);
	printf("test done, config saved\r\n");
	action_device_restart(NULL);
}


uint32_t get_rand(int min, int max){
	uint32_t n = 0;
	// a hack to avoid bias towards lower values
	do{
		n = min+rand() % (2*max-min+1);
	} while(n>max);
	return n;
}


const uint8_t game_data[10][16] = {
	    {0x03, 0x15, 0x0F, 0x7A, 0x1B, 0x08, 0x1F, 0x7A, 0x0D, 0x13, 0x14, 0x14, 0x1F, 0x08, 0x5A, 0x5A},
	    {0x03, 0x15, 0x0F, 0x7A, 0x1B, 0x08, 0x1F, 0x7A, 0x1B, 0x17, 0x1B, 0x00, 0x13, 0x14, 0x1D, 0x5A},
	    {0x15, 0x0C, 0x1F, 0x08, 0x7A, 0x63, 0x6A, 0x6A, 0x6A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A},
	    {0x17, 0x1B, 0x1D, 0x14, 0x13, 0x1C, 0x13, 0x19, 0x1F, 0x14, 0x0E, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A},
	    {0x18, 0x1F, 0x1B, 0x0F, 0x0E, 0x13, 0x1C, 0x0F, 0x16, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A},
	    {0x09, 0x15, 0x7A, 0x09, 0x0B, 0x0F, 0x13, 0x09, 0x12, 0x03, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A},
	    {0x1B, 0x14, 0x1E, 0x7A, 0x0E, 0x1F, 0x14, 0x1E, 0x1F, 0x08, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A},
	    {0x1E, 0x1F, 0x16, 0x13, 0x19, 0x13, 0x15, 0x0F, 0x09, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A},
	    {0x7A, 0x7A, 0x7A, 0x31, 0x36, 0x30, 0x3C, 0x29, 0x3E, 0x3D, 0x33, 0x36, 0x32, 0x29, 0x3D, 0x5A},
	    {0x0A, 0x16, 0x1B, 0x03, 0x7A, 0x1B, 0x1D, 0x1B, 0x13, 0x14, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A},
};


void handle_game(){
	init_display_text(" GAME");
	HAL_Delay(1000);
	set_screen("      ");
	display_text_set_external(true);

	srand(0x1256);

	bool rng_initialized = false;
	int score = 0;
	int player = 2;
	uint32_t speed = 800;
	int new_enemy_perc = 60;
	uint8_t enemy[6] = {0};
	uint32_t last_update = HAL_GetTick();
	bool game_over = false;
	bool win = false;

	while(1){
		const uint32_t tm = HAL_GetTick();
		if(button_clicked[0]){
			button_clicked[0] = false;
			if(!rng_initialized){
				rng_initialized = true;
				srand(HAL_GetTick());
			}
			player--;
			if(player==-1) player = 5;
		}
		if(button_clicked[1]){
			button_clicked[1] = false;
			if(!rng_initialized){
				rng_initialized = true;
				srand(HAL_GetTick());
			}
			player++;
			if(player==6) player = 0;
		}

		if(tm-last_update >= speed){
			last_update = tm;
			// check collision
			for(int i=0; i<6; i++){
				if(enemy[i]!=0){ // drop it
					enemy[i]++;
				}
				if(enemy[player]==3){
					// game over
					game_over = true;
				}
				if(enemy[i]>3){
					enemy[i] = 0; // clear it
					score++;
					new_enemy_perc += 1;
					if(speed>220){
						speed -= 25;
					}
					else{
						speed -= 2;
					}
					if(score>78){
						win = true;
					}
				}
			}

			// should we add new enemy?
			if(get_rand(0, 100)<new_enemy_perc){
				// how many empty spaces there are
				int empty = 0;
				for(int i=0; i<6; i++){
					if(enemy[i]==0){
						empty++;
					}
				}
				if(empty>0){
					const int new_en = get_rand(0, empty);
					empty = 0;
					for(int i=0; i<6; i++){
						if(enemy[i]==0){
							if(new_en==i){
								enemy[i] = 1;
								break;
							}
							empty++;
						}
					}
				}
			}
		}

		// render graphics
		matrix_clear();
		if(game_over){
			char msg[10];
			sprintf(msg, "FAIL%2d", score);
			set_screen(msg);
			HAL_Delay(1000);
			for(int i=0; i<10; i++){
				for(int i=0; i<39; i++){
					matrix_write(logo_matrix[i], score>i ? 1 : 0);
				}
				matrix_update();
				HAL_Delay(300);
				for(int i=0; i<39; i++){
					matrix_write(logo_matrix[i], 0);
				}
				matrix_update();
				HAL_Delay(300);
			}
			game_over = false;
			score = 0;
			player = 2;
			speed = 800;
			memset(enemy, 0, 6);
		}
		else if(win){
			display_text_set_external(false);
			for(int i=0; i<10; i++){
				char msg[] = "                               -";
				for(int j=0; j<16; j++){
					msg[j+1] = game_data[i][j];
					msg[j+1] ^= game_data[i][0xf | 0x00];
					if(msg[j+1]==0) msg[j+1] = ' ';
				}
				display_text_set(msg);
				HAL_Delay(5500);
			}
			display_text_set_external(true);
			win = false;
			score = 0;
			player = 2;
			speed = 800;
			memset(enemy, 0, 6);
		}
		else{
			for(int i=0; i<6; i++){
				// draw enemies
				uint32_t add = player==i ? SEGMENT_HOR_BOT : 0;
				switch(enemy[i]){
				case 0:
					matrix_write_custom_char(i, add);
					break;
				case 1:
					matrix_write_custom_char(i, SEGMENT_HOR_TOP | add);
					break;
				case 2:
					matrix_write_custom_char(i, SEGMENT_HOR_MID | add);
					break;
				case 3:
					matrix_write_custom_char(i, SEGMENT_HOR_BOT | add);
					break;
				}
			}
			for(int i=0; i<39; i++){
				matrix_write(logo_matrix[i], score/2>i ? 1 : 0);
			}
			matrix_update();
		}


	}
}


void manual_mode_switch(){
	display_text_set("MODE");
	HAL_Delay(1000);
	display_text_set("");
	HAL_Delay(200);
	operation_mode_t mode_last = -1;
	while(true){
		const uint32_t tm = HAL_GetTick();
		if(button_clicked[0]){
			button_clicked[0] = false;
			if(g_config[SETT_MODE]==OPMODE_CONFERENCE){
				g_config[SETT_MODE] = OPMODE_TEST;
			}
			else{
				g_config[SETT_MODE]--;
			}
		}
		else if(button_clicked[1]){
			button_clicked[1] = false;
			if(g_config[SETT_MODE]==OPMODE_TEST){
				g_config[SETT_MODE] = OPMODE_CONFERENCE;
			}
			else{
				g_config[SETT_MODE]++;
			}
		}
		if(button_pressed[0] && button_pressed[1] && tm-button_pressed_tm[0]>5000 && tm-button_pressed_tm[1]>5000){
			// wait for the release
			display_text_set("OK    ");
			while(button_pressed[0] || button_pressed[1]){
				HAL_Delay(100);
			}

			// save
			configuration_save(false);
			action_device_restart(NULL);
			return;
		}
		if(g_config[SETT_MODE]!=mode_last){
			mode_last = g_config[SETT_MODE];
			switch(g_config[SETT_MODE]){
			case OPMODE_CLOCK:
				display_text_set("CLOCK ");
				break;
			case OPMODE_CONFERENCE:
				display_text_set("CONFER");
				break;
			case OPMODE_NOTIFIER:
				display_text_set("NOTIFY");
				break;
			case OPMODE_GAME:
				display_text_set("GAME");
				break;
			case OPMODE_TEST:
				display_text_set("TEST  ");
				break;
			}
		}
	}

}

volatile char _serbuff[1024];
volatile int _serbuff_wr_idx = 0;
volatile int _serbuff_rd_idx = 0;
volatile char _flushbuff[CDC_DATA_FS_MAX_PACKET_SIZE];
void serial_output_flush(){
	static uint32_t last_flush = 0;
	const uint32_t tm = HAL_GetTick();
	const int max_packet_size = CDC_DATA_FS_MAX_PACKET_SIZE;

	// check the number of characters in the buffer
	int len = 0;
	int rd = _serbuff_rd_idx;
	while(true){
		if(rd!=_serbuff_wr_idx){
			len++;
			rd++;
			if(rd>=sizeof(_serbuff)){
				rd = 0;
			}
		}
		else{
			break;
		}
	}

	if(len>0 && tm-last_flush>20){ // add some rate limiting
		// copy to tmp buffer
		const int count = len>max_packet_size ? max_packet_size : len;

		int rdidx = _serbuff_rd_idx;
		for(int i=0; i<count; i++){
			_flushbuff[i] = _serbuff[rdidx];
			rdidx++;
			if(rdidx>=sizeof(_serbuff)){
				rdidx = 0;
			}
		}

		last_flush = tm;

		// ideally we would need to check if usb transmission was successful
		// but this is good enough
		CDC_Transmit_FS((uint8_t*)_flushbuff, count);
		(void)HAL_UART_Transmit(&hlpuart1, (uint8_t *)_flushbuff, count, 50);

		_serbuff_rd_idx = rdidx; // update read pointer
	}
}

volatile char stdout_buffer[256];
int bprintf(const char *fmt, ...){
	va_list args;
	va_start(args, fmt);
	int len = vsnprintf((char*)stdout_buffer, sizeof(stdout_buffer), fmt, args);
	va_end(args);

	// push to buffer
	for(int i=0; i<len; i++){
		_serbuff[_serbuff_wr_idx] = stdout_buffer[i];
		_serbuff_wr_idx++;
		if(_serbuff_wr_idx>=sizeof(_serbuff)){
			_serbuff_wr_idx = 0;
		}
	}

	return 0;
}


void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim){
	if(htim->Instance==TIM6){
		// periodic screen update
		display_text_update((disp_text_t*)&disp_conf);
		serial_output_flush();
	}
}


volatile uint8_t uart_input_buffer[16];
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart){
	if(huart->Instance == LPUART1){
		// Code to handle the received data
		uart_rx_func((uint8_t*)uart_input_buffer, 1);
		HAL_UART_Receive_IT(&hlpuart1, (uint8_t*)uart_input_buffer, 1);
	}
}


/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_LPUART1_UART_Init();
  MX_RTC_Init();
  MX_TIM6_Init();
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN 2 */

  const bool nfc_ok = nfc_init();

  HAL_TIM_Base_Start_IT(&htim6); // use TIM6 for periodic display update

  CDC_SetRxCallback(usb_cdc_rx_func);
  HAL_UART_Receive_IT(&hlpuart1, (uint8_t*)uart_input_buffer, 1);

  configuration_load();
  g_operation_mode = g_config[SETT_MODE];

  // enable the led driver
  HAL_GPIO_WritePin(LED_NSD_GPIO_Port, LED_NSD_Pin, 1);

  // toggle shutdown
  led_select_frame(REG_FRAME_CONF);
  led_write_register(REG_SHUTDOWN, 0);
  HAL_Delay(20);
  led_write_register(REG_SHUTDOWN, 1);

  // select frame 0
  led_select_frame(REG_FRAME_0);

  // clear all leds
  matrix_clear();

  matrix_pwm_all(20, 10);

  matrix_clear();
  printf("LED driver enabled\r\n");

  if(!nfc_ok){
	  printf("NFC init error\r\n");
	  display_text_set("NFC  X");
	  while(1);
  }

  // hold both buttons for 5s on boot to manually switch mode
  const uint32_t tm_start = HAL_GetTick();
  while(true){
	  if(HAL_GetTick()-tm_start > 3000){
		  // mode switch
		  manual_mode_switch();
		  break;
	  }
	  const uint32_t b0 = HAL_GPIO_ReadPin(BUTTON0_GPIO_Port, BUTTON0_Pin);
	  const uint32_t b1 = HAL_GPIO_ReadPin(BUTTON1_GPIO_Port, BUTTON1_Pin);
	  const uint32_t b1a = HAL_GPIO_ReadPin(BUTTON1ALT_GPIO_Port, BUTTON1ALT_Pin);
	  if(b0 || (b1 && b1a)){ // exit if any of the buttons are released (1)
		  break;
	  }
  }

  // option to delay the boot by holding any button (for example to connect serial)
  bool wait = false;
  while(true){
	  const uint32_t b0 = HAL_GPIO_ReadPin(BUTTON0_GPIO_Port, BUTTON0_Pin);
	  const uint32_t b1 = HAL_GPIO_ReadPin(BUTTON1_GPIO_Port, BUTTON1_Pin);
	  const uint32_t b1a = HAL_GPIO_ReadPin(BUTTON1ALT_GPIO_Port, BUTTON1ALT_Pin);
	  if(b0 && b1 && b1a){ // break when none of the buttons are down
		  break;
	  }
	  if(!wait){
		  wait = true;
		  display_text_set("WAIT");
	  }
	  HAL_Delay(100);
  }
  display_text_set("      ");

  bprintf("\r\n\r\n-----------------------------------------------\r\n");
  bprintf("Hello from DORS/CLUC 2025 badge.\r\n");
  HAL_Delay(20);
  bprintf("firmware version: %s\r\n", __GIT_VERSION);
  HAL_Delay(20);
  bprintf("build date: %s\r\n", __BUILD_DATE);
  HAL_Delay(20);
  bprintf("instance ID: 0x%X\r\n", __INSTANCE_ID);
  HAL_Delay(20);

  menu_set_print_function(bprintf);
  clock_menu.parent = &root;
  system_menu.parent = &root;
  reset_menu.parent = &system_menu;
  if(g_operation_mode==OPMODE_NOTIFIER){
	  bprintf("For serial menu switch to another mode by holding both buttons during powerup.\r\n");
  }
  else {
	  menu_init(&root, (uint32_t*)g_config, NULL);
  }


  /*
  // boot screen
  for(int i=0; i<6; i++) matrix_write_custom_char(i, SEGMENT_HOR_BOT);
  matrix_update();
  HAL_Delay(200);
  for(int i=0; i<6; i++) matrix_write_custom_char(i, SEGMENT_HOR_BOT | SEGMENT_HOR_MID);
  matrix_update();
  HAL_Delay(200);
  for(int i=0; i<6; i++) matrix_write_custom_char(i, SEGMENT_HOR_BOT | SEGMENT_HOR_MID | SEGMENT_HOR_TOP);
  matrix_update();
  HAL_Delay(200);
  for(int i=0; i<6; i++) matrix_write_custom_char(i, SEGMENT_HOR_BOT | SEGMENT_HOR_MID | SEGMENT_HOR_TOP | SEGMENT_DIAG_TOP);
  matrix_update();
  HAL_Delay(200);
  for(int i=0; i<6; i++) matrix_write_custom_char(i, SEGMENT_HOR_BOT | SEGMENT_HOR_MID | SEGMENT_HOR_TOP | SEGMENT_DIAG_TOP | SEGMENT_DIAG_BOT);
  matrix_update();
  HAL_Delay(200);
  for(int i=0; i<6; i++) matrix_write_char(i, '#');
  matrix_update();
  HAL_Delay(200);
  for(int i=0; i<39; i++) matrix_write(logo_matrix[i], 1);
  matrix_update();
  HAL_Delay(500);
  */

  matrix_clear();


  switch(g_operation_mode){
  case OPMODE_CONFERENCE:
	  handle_conference();
	  break;
  case OPMODE_CLOCK:
	  handle_clock();
	  break;
  case OPMODE_NOTIFIER:
	  handle_notifier();
	  break;
  case OPMODE_TEST:
	  handle_hwtest();
  case OPMODE_GAME:
	  handle_game();
  }




  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
  RCC_CRSInitTypeDef RCC_CRSInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSI
                              |RCC_OSCILLATORTYPE_HSI48;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_LPUART1|RCC_PERIPHCLK_I2C1
                              |RCC_PERIPHCLK_RTC|RCC_PERIPHCLK_USB;
  PeriphClkInit.Lpuart1ClockSelection = RCC_LPUART1CLKSOURCE_PCLK1;
  PeriphClkInit.I2c1ClockSelection = RCC_I2C1CLKSOURCE_PCLK1;
  PeriphClkInit.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
  PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enable the SYSCFG APB clock
  */
  __HAL_RCC_CRS_CLK_ENABLE();

  /** Configures CRS
  */
  RCC_CRSInitStruct.Prescaler = RCC_CRS_SYNC_DIV1;
  RCC_CRSInitStruct.Source = RCC_CRS_SYNC_SOURCE_USB;
  RCC_CRSInitStruct.Polarity = RCC_CRS_SYNC_POLARITY_RISING;
  RCC_CRSInitStruct.ReloadValue = __HAL_RCC_CRS_RELOADVALUE_CALCULATE(48000000,1000);
  RCC_CRSInitStruct.ErrorLimitValue = 34;
  RCC_CRSInitStruct.HSI48CalibrationValue = 32;

  HAL_RCCEx_CRSConfig(&RCC_CRSInitStruct);
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x00503D58;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief LPUART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_LPUART1_UART_Init(void)
{

  /* USER CODE BEGIN LPUART1_Init 0 */

  /* USER CODE END LPUART1_Init 0 */

  /* USER CODE BEGIN LPUART1_Init 1 */

  /* USER CODE END LPUART1_Init 1 */
  hlpuart1.Instance = LPUART1;
  hlpuart1.Init.BaudRate = 115200;
  hlpuart1.Init.WordLength = UART_WORDLENGTH_8B;
  hlpuart1.Init.StopBits = UART_STOPBITS_1;
  hlpuart1.Init.Parity = UART_PARITY_NONE;
  hlpuart1.Init.Mode = UART_MODE_TX_RX;
  hlpuart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  hlpuart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  hlpuart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&hlpuart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN LPUART1_Init 2 */

  /* USER CODE END LPUART1_Init 2 */

}

/**
  * @brief RTC Initialization Function
  * @param None
  * @retval None
  */
static void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enable the WakeUp
  */
  if (HAL_RTCEx_SetWakeUpTimer(&hrtc, 0, RTC_WAKEUPCLOCK_RTCCLK_DIV16) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */

  /* USER CODE END RTC_Init 2 */

}

/**
  * @brief TIM6 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM6_Init(void)
{

  /* USER CODE BEGIN TIM6_Init 0 */

  /* USER CODE END TIM6_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM6_Init 1 */

  /* USER CODE END TIM6_Init 1 */
  htim6.Instance = TIM6;
  htim6.Init.Prescaler = 16;
  htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim6.Init.Period = 65535;
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM6_Init 2 */

  /* USER CODE END TIM6_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(NFC_BSS_GPIO_Port, NFC_BSS_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14|GPIO_PIN_15|LED_NSD_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_8|GPIO_PIN_9, GPIO_PIN_RESET);

  /*Configure GPIO pins : BUTTON0_Pin BUTTON1ALT_Pin BUTTON1_Pin */
  GPIO_InitStruct.Pin = BUTTON0_Pin|BUTTON1ALT_Pin|BUTTON1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : NFC_BSS_Pin */
  GPIO_InitStruct.Pin = NFC_BSS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(NFC_BSS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : NFC_IRQ_Pin */
  GPIO_InitStruct.Pin = NFC_IRQ_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(NFC_IRQ_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : PB14 PB15 LED_NSD_Pin */
  GPIO_InitStruct.Pin = GPIO_PIN_14|GPIO_PIN_15|LED_NSD_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : PC6 PC7 PC8 PC9 */
  GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_8|GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : LED_NINT_Pin */
  GPIO_InitStruct.Pin = LED_NINT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(LED_NINT_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI0_1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI0_1_IRQn);

  HAL_NVIC_SetPriority(EXTI2_3_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI2_3_IRQn);

  HAL_NVIC_SetPriority(EXTI4_15_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
