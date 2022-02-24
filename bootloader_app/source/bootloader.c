#include "bootloader.h"

// TODO:
// bootloader should have a config file with this value or be given thiat value etc
#define USER_APP_LOCATION (0x8020000 + 4)


// buffer frame where we will store recevied bytes directly from UART
uint8_t bytes_buff[sizeof(frame_format_t)] = {0};
// frame we will assemble from received bytes
frame_format_t receivedFrame;
uint8_t bytes_received_count = 0;
// TODO: should this be sent by the cmoputer app?
// let the user chose where the firmware will decide?
// however the binary feel needs to have the propper
// vector table offset...so maybe not
uint32_t start_address = 0x8020000;
uint32_t blockNum = 0;
bootloader_state bootlaoder_current_state = STATE_IDLE;

// TODO:
bool parse = false;
// TODO:
// bootloader should  have no knowledge of this, atleast not explicitly
extern UART_HandleTypeDef huart2;
// private prototypes
static void print(char *msg, ...);
static void jump_to_user_app(void);
static void write_payload(void);
static void erase_sector(void);
static bool parse_frame(void);
static void reset_recevied_frame(void);
static void set_bl_state(bootloader_state state);
// state function protoypes
frame_format_t updating_state_func(void);
frame_format_t idle_state_func(void);
frame_format_t start_update_state_func(void);
void bootloader_main(void)
{
	// TODO: fix :enable RX interrupt
	USART2->CR1 |= USART_CR1_RXNEIE;
    //erase_sector();
	// initialize state functions
	bootloader_state_functions[STATE_IDLE] = idle_state_func;
	bootloader_state_functions[STATE_START_UPDATE] = start_update_state_func;
	bootloader_state_functions[STATE_UPDATING] = updating_state_func;

	//initialize state again.... just to be sure
	bootlaoder_current_state = STATE_IDLE;

	while (1)
	{
		(*bootloader_state_functions[bootlaoder_current_state])();

		// HAL_GPIO_TogglePin(GPIOA, user_led_Pin);

	}
}
frame_format_t start_update_state_func(void)
{
	//clear whatever needs to be cleared
	parse = false; 
	//this will have STATE_START_UPDATE frame
	reset_recevied_frame();

	set_bl_state(STATE_UPDATING); 

	//send ack
	print("o");
}
frame_format_t updating_state_func(void)
{
	//once we are updating for sure
	//we can go ahead and erase the required sectors only once
	static bool erased = false;
	if(parse_frame())
	{
		if (receivedFrame.frame_id == BL_PAYLOAD)
		{
			if(!erased) //only do this once
			{
				erase_sector();
				erased = true;
			}
			write_payload();
		}
		else if (receivedFrame.frame_id == BL_UPDATE_DONE)
		{
			jump_to_user_app();
		}

	}

}
frame_format_t idle_state_func(void)
{
	if(parse_frame())
	{

		switch (receivedFrame.frame_id)
		{
			case BL_START_UPDATE:
			set_bl_state(STATE_START_UPDATE); 
			break;

			//only states above are valie to switch out of idle state
			default : 
			set_bl_state(STATE_IDLE); 
		}
	}
}
static bool parse_frame(void)
{
	//checks if we have a frame to parse
	if (parse)
	{
		
		// assemble a frame from bytes_buff
		memcpy(&receivedFrame, bytes_buff, sizeof(frame_format_t));

		// the type of frame we get will dictate what the next state should be
		if (receivedFrame.start_of_frame == BL_START_OF_FRAME && receivedFrame.end_of_frame == BL_END_OF_FRAME)
		{
			//TODO: check CRC
			//if frame is valid 
			return true;
			
		}		
	}
	return false;
}
static void set_bl_state(bootloader_state state)
{
	bootlaoder_current_state = state; 	
}
static void write_payload(void)
{

	HAL_FLASH_Unlock();
	for (int i = 0; i < 16; i += 4)
	{
		uint32_t *val = (uint32_t *)&receivedFrame.payload[i];
		HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, start_address, *val);
		start_address += 4;
	}
	HAL_FLASH_Unlock();
	//clear receivedFrame
	receivedFrame.start_of_frame = 0;
	receivedFrame.frame_id = 0;
	receivedFrame.payload_len = 0;
	receivedFrame.crc32 = 0;
	receivedFrame.end_of_frame = 0;
	for(int i = 0 ; i< 16;i++)
	{
		receivedFrame.payload[i] = 0;
	}
	//TODO: read back the data and check crc

	//this print will change to be an ack once crc read checks out ok
	parse = false;
	print("o");

	//	HAL_FLASH_Lock();
}

static void jump_to_user_app(void)
{
	void (*user_app_reset_handler)(void) = (void *)(*((uint32_t *)(USER_APP_LOCATION)));
	user_app_reset_handler();
}
static void reset_recevied_frame(void)
{
	receivedFrame.start_of_frame = 0;
	receivedFrame.frame_id = 0;
	receivedFrame.payload_len = 0;
	receivedFrame.crc32 = 0;
	receivedFrame.end_of_frame = 0;
	for(int i = 0 ; i< 16;i++)
	{
		receivedFrame.payload[i] = 0;
	}
}
static void print(char *msg, ...)
{
	char buff[250];
	va_list args;
	va_start(args, msg);
	vsprintf(buff, msg, args);

	for (int i = 0; i < strlen(buff); i++)
	{
		USART2->DR = buff[i];
		while (!(USART2->SR & USART_SR_TXE))
			;
	}

	while (!(USART2->SR & USART_SR_TC))
		;
}
// TODO: lots to do here keep playing with it....
void bootloader_USART2_callback(uint8_t data)
{
	// filll buffer until we have enough bytes to assemble a frame
	if (bytes_received_count <= sizeof(frame_format_t))
	{
		bytes_buff[bytes_received_count++] = data;
		if (bytes_received_count == sizeof(frame_format_t))
		{

			bytes_received_count = 0;
			parse = true;
		}
	}
	// USART2->DR = data; //echo the data
	//  HAL_GPIO_TogglePin(GPIOA, user_led_Pin);
}

// TODO:  abstract sector erasing based user app memory locationa and size
void erase_sector(void)
{

	FLASH_EraseInitTypeDef erase;
	erase.NbSectors = 1;
	erase.Sector = FLASH_SECTOR_5;
	erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
	uint32_t err = 0;

	HAL_FLASH_Unlock();
	HAL_FLASHEx_Erase(&erase, &err);
	HAL_FLASH_Lock();
}
