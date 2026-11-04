/*
	File:		host/esp32/main/tdeck_board.cpp

	Contains:	Which board this is (tdeck_board.h): the I2C bus every chip
				on it shares, the T-Deck Max told from the T-Deck Pro by its
				XL9555 expander answering at 0x20 (a Pro has none, of either
				revision), and the lines each driver switches.

				The XL9555 is a 16-line expander with the registers of the
				PCA9555 family: outputs in 2 and 3, directions in 6 and 7 (a
				0 an output).  Its outputs outlast a reset of the ESP32-S3,
				and every line starts as an input, which LilyGO's boards read
				as pulled high, so the output registers are written before
				the directions: a line must not go from pulled up to its
				value through a glitch on a reset line.  What each is: the
				schematic (T-Deck Pro Max V0.1, sheet 7) and LilyGO's
				TDeckMaxBoard.h.
				ESP-IDF's headers only (esp32host.h says why).
*/

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"

#include "esp32host.h"
#include "tdeck_board.h"

#define I2C_PORT		I2C_NUM_0
#define PIN_SDA			13
#define PIN_SCL			14
#define ADDR_EXPANDER	0x20
#define ADDR_MOTOR		0x5A
#define PIN_FRONTLIGHT	41

bool	gBoardMax = false;

enum
{
	kExpanderOutput = 2,				// and 3
	kExpanderDirection = 6				// and 7
};

// where each line is: a GPIO on the Pro (-1: it has none), the expander's
// line on the Max (0-7 port 0, 8-15 port 1; -1: none)
struct LinePlace
{
	int		pro;
	int		max;
};
static const LinePlace	kLines[kLineCount] = {
	{ 41, 0 },							// kLineModemPower: BOARD_6609_EN, P00
	{ 40, 8 },							// kLineModemKey: P10
	{ 46, 1 },							// kLineLoraPower: BOARD_LORA_EN, P01
	{ 39, 2 },							// kLineGpsPower: BOARD_GPS_EN, P02
	{ 45, 7 },							// kLineTouchReset: P07
	{ -1, 9 },							// kLineKeyboardReset: P11
	{ -1, 3 },							// kLineImuPower: P03
	{ -1, 4 },							// kLineLoraAntenna: P04
	{ -1, 5 },							// kLineMotorPower: P05
	{ -1, 6 },							// kLineAmplifier: P06, the net SHUTDOWM
	{ -1, 10 }							// kLineAudioFromModem: P12
};

static SemaphoreHandle_t	gExpanderLock = NULL;
static uint16_t				gOutputs = 0;			// what the expander drives, as written
static bool					gLineLevels[kLineCount];


static bool
ExpanderWrite(uint8_t reg, uint16_t value)
{
	uint8_t b[3] = { reg, (uint8_t) value, (uint8_t) (value >> 8) };		// (the port pair in one write)
	return i2c_master_write_to_device(I2C_PORT, ADDR_EXPANDER, b, 3, pdMS_TO_TICKS(20)) == ESP_OK;
}


static bool
ExpanderFound(void)
{
	uint8_t reg = kExpanderDirection, direction[2];
	return i2c_master_write_read_device(I2C_PORT, ADDR_EXPANDER, &reg, 1, direction, 2, pdMS_TO_TICKS(20)) == ESP_OK;
}


// Every line an output, at rest: the radios and sensors off, the loudspeaker's
// amplifier off (tdeck_audio.cpp has it on while there is sound), the codec
// on the loudspeaker, the internal antenna, nothing held in reset.
static bool
ExpanderStart(void)
{
	gExpanderLock = xSemaphoreCreateMutex();
	gOutputs = 0;
	const BoardLine high[] = { kLineTouchReset, kLineKeyboardReset, kLineLoraAntenna };
	for (BoardLine line : high)
	{
		gOutputs |= (uint16_t) (1 << kLines[line].max);
		gLineLevels[line] = true;
	}
	return ExpanderWrite(kExpanderOutput, gOutputs) && ExpanderWrite(kExpanderDirection, 0xF800);	// (P13-P17 are not connected)
}


bool
HostDeviceBoardStart(void)
{
	i2c_config_t c;
	memset(&c, 0, sizeof(c));
	c.mode = I2C_MODE_MASTER;
	c.sda_io_num = PIN_SDA;
	c.scl_io_num = PIN_SCL;
	c.sda_pullup_en = GPIO_PULLUP_ENABLE;
	c.scl_pullup_en = GPIO_PULLUP_ENABLE;
	c.master.clk_speed = 400000;
	if (i2c_param_config(I2C_PORT, &c) != ESP_OK || i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0) != ESP_OK)
	{
		printf("newton: no I2C - no touch panel or keyboard\n");
		return false;
	}
	// the fuel gauge (tdeck_battery.cpp) stretches the clock while it wakes,
	// far longer than the bus's own timeout (2^10 cycles of its 40 MHz
	// clock, 26 us): 2^19 cycles, 13 ms
	i2c_set_timeout(I2C_PORT, 19);
	gBoardMax = ExpanderFound();
	if (gBoardMax && !ExpanderStart())
		printf("newton: the XL9555 answered, then would not take its lines\n");
	printf("newton: a T-Deck %s\n", gBoardMax ? "Max" : "Pro");
	return true;
}


void
HostDeviceBoardLineStart(BoardLine line)
{
	int pin = kLines[line].pro;
	if (gBoardMax || pin < 0)
		return;
	// (a firmware that put the board to sleep may have latched the pin with
	// GPIO hold - lilyphone1's does - which outlasts a reset and ignores
	// every write until released)
	gpio_hold_dis((gpio_num_t) pin);
	gpio_config_t pins = {};
	pins.pin_bit_mask = 1ULL << pin;
	pins.mode = GPIO_MODE_INPUT_OUTPUT;			// (read back for the console)
	gpio_config(&pins);
}


void
HostDeviceBoardSet(BoardLine line, bool high)
{
	gLineLevels[line] = high;
	if (!gBoardMax)
	{
		if (kLines[line].pro >= 0)
			gpio_set_level((gpio_num_t) kLines[line].pro, high ? 1 : 0);
		return;
	}
	xSemaphoreTake(gExpanderLock, portMAX_DELAY);
	uint16_t bit = (uint16_t) (1 << kLines[line].max);
	gOutputs = high ? (gOutputs | bit) : (gOutputs & ~bit);
	if (!ExpanderWrite(kExpanderOutput, gOutputs))
		printf("newton: the XL9555 did not take line %d\n", (int) line);
	xSemaphoreGive(gExpanderLock);
}


bool
HostDeviceBoardGet(BoardLine line)
{
	return gLineLevels[line];
}


/*------------------------------------------------------------------------------
	The frontlight: PWM on GPIO 41, the higher the brighter.  It stops in light
	sleep, so the chip stays awake while it is on.
------------------------------------------------------------------------------*/

static long		gFrontlight = 0;
static bool		gFrontlightLocked = false;


static void
FrontlightApply(void)
{
	static bool started = false;
	long level = gFrontlightLocked ? 0 : gFrontlight;
	if (!started)
	{
		if (level == 0)
			return;
		ledc_timer_config_t timer = {};
		timer.speed_mode = LEDC_LOW_SPEED_MODE;
		timer.duty_resolution = LEDC_TIMER_8_BIT;
		timer.timer_num = LEDC_TIMER_0;
		timer.freq_hz = 5000;
		timer.clk_cfg = LEDC_AUTO_CLK;
		ledc_channel_config_t channel = {};
		channel.gpio_num = PIN_FRONTLIGHT;
		channel.speed_mode = LEDC_LOW_SPEED_MODE;
		channel.channel = LEDC_CHANNEL_0;
		channel.timer_sel = LEDC_TIMER_0;
		if (ledc_timer_config(&timer) != ESP_OK || ledc_channel_config(&channel) != ESP_OK)
			return;
		started = true;
	}
	HostDevicePowerKeepAwake(kAwakeLight, level != 0);
	ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (uint32_t) (level * 255 / 100));
	ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}


bool
HostDeviceFrontlightSet(long level)
{
	if (!gBoardMax)
		return false;
	gFrontlight = level < 0 ? 0 : level > 100 ? 100 : level;
	FrontlightApply();
	return true;
}


long
HostDeviceFrontlight(void)
{
	return gFrontlight;
}


void
HostDeviceFrontlightLocked(bool locked)
{
	if (!gBoardMax)
		return;
	gFrontlightLocked = locked;
	FrontlightApply();
}


/*------------------------------------------------------------------------------
	The vibration motor: a DRV2605 (enabled by kLineMotorPower), set up as
	LilyGO's firmware has it - an ERM motor, open loop, the first effect
	library, internal trigger.  Left in standby between buzzes.
------------------------------------------------------------------------------*/

static bool
MotorWrite(uint8_t reg, uint8_t value)
{
	uint8_t b[2] = { reg, value };
	return i2c_master_write_to_device(I2C_PORT, ADDR_MOTOR, b, 2, pdMS_TO_TICKS(20)) == ESP_OK;
}


static void
MotorRest(void*)
{
	MotorWrite(0x01, 0x40);				// MODE: standby
}


void
HostDeviceBuzz(int effect)
{
	static bool started = false;
	static esp_timer_handle_t rest = NULL;
	if (!gBoardMax)
		return;
	if (!started)
	{
		HostDeviceBoardSet(kLineMotorPower, true);
		vTaskDelay(pdMS_TO_TICKS(10));
		esp_timer_create_args_t timer = {};
		timer.callback = MotorRest;
		timer.name = "motor";
		esp_timer_create(&timer, &rest);
		uint8_t reg = 0x1A, feedback = 0, control = 0;	// FEEDBACK: bit 7 clear, ERM
		i2c_master_write_read_device(I2C_PORT, ADDR_MOTOR, &reg, 1, &feedback, 1, pdMS_TO_TICKS(20));
		MotorWrite(0x1A, feedback & 0x7F);
		reg = 0x1D;										// CONTROL3: ERM open loop
		i2c_master_write_read_device(I2C_PORT, ADDR_MOTOR, &reg, 1, &control, 1, pdMS_TO_TICKS(20));
		MotorWrite(0x1D, control | 0x20);
		MotorWrite(0x03, 1);							// LIBRARY: A
		uint8_t status = 0;
		reg = 0x00;
		bool found = i2c_master_write_read_device(I2C_PORT, ADDR_MOTOR, &reg, 1, &status, 1, pdMS_TO_TICKS(20)) == ESP_OK;
		printf("newton: the DRV2605 %s, status %02X, feedback %02X, control %02X\n", found ? "answers" : "does not answer", status, feedback, control);
		started = true;
	}
	MotorWrite(0x01, 0x00);				// MODE: out of standby, internal trigger
	MotorWrite(0x04, (uint8_t) effect);	// the effect (the DRV2605's own numbers)
	MotorWrite(0x05, 0);				// and the end
	MotorWrite(0x0C, 1);				// GO
	esp_timer_stop(rest);
	esp_timer_start_once(rest, 1000 * 1000);
}
