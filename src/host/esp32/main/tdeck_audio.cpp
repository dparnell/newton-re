/*
	File:		host/esp32/main/tdeck_audio.cpp

	Contains:	The T-Deck Max's loudspeaker and microphone: host/HostAudio.h's
				six calls over an ES8311 codec on I2S (a Pro has no codec:
				HostAudioOpen answers false there, and the Newton runs without
				a sound driver as it always has).

				The Newton's sound server plays at 21600 samples a second,
				the MP2x00's top rate, so the I2S clocks run at that with
				MCLK 256 times it, which is one line of the ES8311's own
				clock table whatever the rate.  The codec is a slave.

				A buffer handed to HostAudioPlay goes into a ring in PSRAM
				(the caller holds the baton and must not wait); a task of
				the board's own takes it from there.  The clocks, the codec
				and the amplifier are only on while there is sound: the first
				samples start them, kIdleMs of silence stops them, so an
				idle board pays nothing and light sleep (held off while they
				run) carries on as before.  The microphone likewise: asked
				for samples it starts, and what has not yet arrived is
				silence; left unasked for kIdleMs it stops.

				The loudspeaker has a second source: the A7682E's voice
				output, through a switch (kLineAudioFromModem).  During a call
				(HostDeviceAudioCall) the switch is the modem's and the
				amplifier stays on, whatever the Newton plays.

				The pins: the schematic (sheet 8) says I2S_ASDOUT is IO17
				and I2S_DSDIN IO40, the codec's own names, so that the ESP32's
				data out is 40 and its data in 17, as LilyGO's examples have
				them.  Its pin map (docs/pinmap.md) has them the other way
				round.

				The codec's registers: the ES8311 user guide in LilyGO's
				T-Deck-MAX repository (hardware/ES8311.user.Guide.pdf), and
				the order Espressif's esp_codec_dev component sets them in.

				ESP-IDF's headers only (esp32host.h says why).
*/

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/i2c.h"
#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

#include "esp32host.h"
#include "tdeck_board.h"
#include "HostAudio.h"

#define I2C_PORT		I2C_NUM_0
#define ADDR_CODEC		0x18
#define PIN_MCLK		38
#define PIN_BCLK		39
#define PIN_LRCK		18
#define PIN_DATA_OUT	40				// ours, to the codec's DSDIN
#define PIN_DATA_IN		17				// ours, from the codec's ASDOUT

const long		kRingSamples = 32768;	// a second and a half each way
const long		kChunk = 512;			// samples moved at a time
const int64_t	kIdleMs = 1500;
const uint8_t	kDacVolume = 0xBF;		// 0 dB (the driver scales the samples for the Newton's volume)
const uint8_t	kMicGain = 0x1A;		// REG14: the analog input on, PGA at +30 dB


// A ring with one writer and one reader
struct Ring
{
	short*				fSamples;
	std::atomic<long>	fIn, fOut;		// counted up, for ever

	long	Filled(void) const	{ return fIn.load() - fOut.load(); }
	long	Room(void) const	{ return kRingSamples - Filled(); }

	void	Put(const short* samples, long count)
	{
		long at = fIn.load();
		for (long i = 0; i < count; i++)
			fSamples[(at + i) % kRingSamples] = samples[i];
		fIn.store(at + count);
	}
	void	Get(short* samples, long count)
	{
		long at = fOut.load();
		for (long i = 0; i < count; i++)
			samples[i] = fSamples[(at + i) % kRingSamples];
		fOut.store(at + count);
	}
};

static Ring					gPlay, gHeard;
static i2s_chan_handle_t	gTx = NULL, gRx = NULL;
static SemaphoreHandle_t	gLock = NULL;			// the hardware switched on or off
static TaskHandle_t			gPlayTask = NULL, gHearTask = NULL;
static bool					gRunning = false;		// the clocks and the codec on
static bool					gPlaying = false, gHearing = false;
static std::atomic<bool>	gInCall(false);
static std::atomic<int64_t>	gLastAsked(0);			// the microphone last asked for samples (ms)
static std::atomic<long>	gDropped(0);


/*------------------------------------------------------------------------------
	The codec
------------------------------------------------------------------------------*/

static bool
CodecWrite(uint8_t reg, uint8_t value)
{
	uint8_t b[2] = { reg, value };
	return i2c_master_write_to_device(I2C_PORT, ADDR_CODEC, b, 2, pdMS_TO_TICKS(20)) == ESP_OK;
}


static bool
CodecWriteAll(const uint8_t (*table)[2], size_t count)
{
	bool good = true;
	for (size_t k = 0; k < count; k++)
		good = CodecWrite(table[k][0], table[k][1]) && good;
	return good;
}


// Up, the clocks already running
static bool
CodecOn(void)
{
	static const uint8_t kOn[][2] = {
		{ 0x44, 0x08 },					// GPIO: I2C noise filter on, no DAC-to-ADC loop
		{ 0x44, 0x08 },					// (twice: the first may be lost as the chip wakes)
		{ 0x01, 0x30 },					// clock manager: MCLK and BCLK on, from the pin
		{ 0x02, 0x00 },					// no pre-divider, no multiplier: MCLK is 256 fs
		{ 0x03, 0x10 },					// ADC: single speed, 16 x oversampling
		{ 0x16, 0x24 },					// ADC scale
		{ 0x04, 0x10 },					// DAC: 16 x oversampling
		{ 0x05, 0x00 },					// ADC and DAC clocks undivided
		{ 0x0B, 0x00 },					// power-up timing
		{ 0x0C, 0x00 },
		{ 0x10, 0x1F },					// analog bias
		{ 0x11, 0x7F },
		{ 0x00, 0x80 },					// out of reset, the codec a slave
		{ 0x01, 0x3F },					// every clock on
		{ 0x06, 0x03 },					// BCLK as it comes
		{ 0x13, 0x10 },					// the output stage on
		{ 0x1B, 0x0A },					// ADC high-pass
		{ 0x1C, 0x6A },
		{ 0x09, 0x0C },					// DAC's serial port: I2S, 16 bits
		{ 0x0A, 0x0C },					// ADC's the same
		{ 0x17, 0xBF },					// ADC volume 0 dB
		{ 0x0E, 0x02 },					// power: the analog PGA and ADC up
		{ 0x12, 0x00 },					// the DAC up
		{ 0x14, kMicGain },
		{ 0x0D, 0x01 },					// power: analog references up
		{ 0x15, 0x40 },					// ADC ramp
		{ 0x37, 0x08 },					// DAC ramp
		{ 0x45, 0x00 },
		{ 0x32, kDacVolume },
		{ 0x31, 0x00 }					// not muted
	};
	return CodecWriteAll(kOn, sizeof(kOn) / sizeof(kOn[0]));
}


// Down to the codec's standby
static void
CodecOff(void)
{
	static const uint8_t kOff[][2] = {
		{ 0x32, 0x00 },					// silent first, so that it does not click
		{ 0x17, 0x00 },
		{ 0x0E, 0xFF },
		{ 0x12, 0x02 },
		{ 0x14, 0x00 },
		{ 0x0D, 0xFA },
		{ 0x15, 0x00 },
		{ 0x02, 0x10 },
		{ 0x00, 0x00 },
		{ 0x00, 0x1F },
		{ 0x01, 0x30 },
		{ 0x01, 0x00 },
		{ 0x45, 0x00 },
		{ 0x0D, 0xFC },
		{ 0x02, 0x00 }
	};
	CodecWriteAll(kOff, sizeof(kOff) / sizeof(kOff[0]));
}


/*------------------------------------------------------------------------------
	Switching it all on and off: for the loudspeaker, the microphone, or both
------------------------------------------------------------------------------*/

static void
Amplifier(void)
{
	HostDeviceBoardSet(kLineAudioFromModem, gInCall);
	HostDeviceBoardSet(kLineAmplifier, gPlaying || gInCall);
}


static void
Need(bool* who, bool on)
{
	xSemaphoreTake(gLock, portMAX_DELAY);
	*who = on;
	bool wanted = gPlaying || gHearing;
	if (wanted && !gRunning)
	{
		HostDevicePowerKeepAwake(kAwakeSound, true);
		i2s_channel_enable(gTx);
		i2s_channel_enable(gRx);
		if (!CodecOn())
			printf("newton: the ES8311 did not take its settings\n");
		gRunning = true;
	}
	Amplifier();
	if (!wanted && gRunning)
	{
		CodecOff();
		i2s_channel_disable(gTx);
		i2s_channel_disable(gRx);
		gRunning = false;
		HostDevicePowerKeepAwake(kAwakeSound, false);
	}
	xSemaphoreGive(gLock);
}


static int64_t
NowMs(void)
{
	return esp_timer_get_time() / 1000;
}


static void
PlayTask(void*)
{
	static short chunk[kChunk];
	for (;;)
	{
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
		if (gPlay.Filled() == 0)
			continue;
		Need(&gPlaying, true);
		int64_t last = NowMs();
		while (NowMs() - last < kIdleMs)
		{
			long count = gPlay.Filled();
			if (count == 0)
			{
				// (the DMA plays silence meanwhile: auto_clear)
				ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
				continue;
			}
			if (count > kChunk)
				count = kChunk;
			gPlay.Get(chunk, count);
			size_t written = 0;
			i2s_channel_write(gTx, chunk, count * sizeof(short), &written, 500);
			last = NowMs();
		}
		Need(&gPlaying, false);
	}
}


static void
HearTask(void*)
{
	static short chunk[kChunk];
	for (;;)
	{
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
		Need(&gHearing, true);
		// (what the DMA holds from before is stale)
		while (NowMs() - gLastAsked.load() < kIdleMs)
		{
			size_t got = 0;
			if (i2s_channel_read(gRx, chunk, sizeof(chunk), &got, 200) != ESP_OK)
				continue;
			// (ESP-IDF 5.1's receive channel gives both slots of a frame, mono
			// or not, here in full duplex - measured with !sound: 43200 samples
			// a second - so every second one is the left's)
			long count = (long) (got / sizeof(short)) / 2;
			for (long i = 0; i < count; i++)
				chunk[i] = chunk[i * 2];
			if (gHeard.Room() >= count)
				gHeard.Put(chunk, count);
		}
		Need(&gHearing, false);
		gHeard.fOut.store(gHeard.fIn.load());
	}
}


static bool
StartTask(void (*entry)(void*), const char* name, TaskHandle_t* handle)
{
	const size_t kStack = 4 * 1024;
	StackType_t* stack = (StackType_t*) heap_caps_malloc(kStack, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	StaticTask_t* control = (StaticTask_t*) heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
	return stack != NULL && control != NULL
		&& (*handle = xTaskCreateStaticPinnedToCore(entry, name, kStack, NULL, 5, stack, control, 0)) != NULL;
}


// The I2S channels (made once): full duplex on one controller, sharing its
// clocks.  Two channels of one port have to be set up alike.
static bool
ChannelsStart(void)
{
	i2s_chan_config_t channels = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
	channels.auto_clear = true;			// (silence when there is nothing to play)
	if (i2s_new_channel(&channels, &gTx, &gRx) != ESP_OK)
		return false;
	i2s_std_config_t config = {
		.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(21600),
		.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
		.gpio_cfg = {
			.mclk = (gpio_num_t) PIN_MCLK,
			.bclk = (gpio_num_t) PIN_BCLK,
			.ws = (gpio_num_t) PIN_LRCK,
			.dout = (gpio_num_t) PIN_DATA_OUT,
			.din = (gpio_num_t) PIN_DATA_IN,
			.invert_flags = { false, false, false }
		}
	};
	config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
	return i2s_channel_init_std_mode(gTx, &config) == ESP_OK
		&& i2s_channel_init_std_mode(gRx, &config) == ESP_OK;
}


static bool
Started(void)
{
	static bool tried = false, good = false;
	if (tried)
		return good;
	tried = true;
	if (!gBoardMax)
		return false;
	gPlay.fSamples = (short*) heap_caps_malloc(kRingSamples * sizeof(short), MALLOC_CAP_SPIRAM);
	gHeard.fSamples = (short*) heap_caps_malloc(kRingSamples * sizeof(short), MALLOC_CAP_SPIRAM);
	gLock = xSemaphoreCreateMutex();
	good = gPlay.fSamples != NULL && gHeard.fSamples != NULL && gLock != NULL
		&& ChannelsStart() && StartTask(PlayTask, "sound", &gPlayTask) && StartTask(HearTask, "hearing", &gHearTask);
	if (!good)
		printf("newton: no sound - no room, or the I2S would not start\n");
	return good;
}


/*------------------------------------------------------------------------------
	host/HostAudio.h
------------------------------------------------------------------------------*/

bool
HostAudioOpen(long /*sampleRate: 21600, kHostSoundRate*/)
{
	return Started();
}


void
HostAudioPlay(const short* samples, long count)
{
	long room = gPlay.Room();
	if (count > room)
	{
		gDropped += count - room;
		count = room;
	}
	gPlay.Put(samples, count);
	xTaskNotifyGive(gPlayTask);
}


void
HostAudioClose(void)
{
}


bool
HostMicrophoneOpen(long /*sampleRate*/)
{
	return Started();
}


void
HostMicrophoneRecord(short* samples, long count)
{
	gLastAsked = NowMs();
	if (!gHearing)
		xTaskNotifyGive(gHearTask);
	long got = gHeard.Filled();
	if (got > count)
		got = count;
	gHeard.Get(samples, got);
	if (got < count)
		memset(samples + got, 0, (count - got) * sizeof(short));
}


void
HostMicrophoneClose(void)
{
}


// a call up or down (tdeck_modem.cpp): the loudspeaker the modem's meanwhile
void
HostDeviceAudioCall(bool on)
{
	if (!gBoardMax || gLock == NULL)
		return;
	gInCall = on;
	xSemaphoreTake(gLock, portMAX_DELAY);
	Amplifier();
	xSemaphoreGive(gLock);
}


void
HostDeviceAudioDump(void)
{
	if (gBoardMax)
		printf("newton: sound %s, %ld played samples dropped, the speaker the %s's\n",
			   gRunning ? "on" : "off", gDropped.load(), gInCall ? "modem" : "codec");
}


// What the microphone hears for ms milliseconds, with a tone of 1 kHz at
// -12 dB playing or not: the mean, the level around it (root mean square, of
// full scale 32767), and how much of that is at 1 kHz (Goertzel's filter).
struct Heard
{
	double	mean, level, tone, rate;
};


static Heard
Listen(long ms, bool tone)
{
	const long kRate = 21600, kPiece = 216;			// (10 ms)
	static short heard[kPiece], played[kPiece];
	const double kCoefficient = 2 * cos(2 * M_PI * 1000.0 / kRate);
	double sum = 0, squares = 0, q1 = 0, q2 = 0;
	long samples = 0, phase = 0;
	int64_t started = 0;
	gHeard.fOut.store(gHeard.fIn.load());			// (nothing from before)
	for (long t = 0; t < ms; t += 10)
	{
		// (kept well ahead of the player, so that the tone has no gaps)
		while (tone && gPlay.Filled() < 8 * kPiece)
		{
			for (long i = 0; i < kPiece; i++, phase++)
				played[i] = (short) (8000.0 * sin(2 * M_PI * 1000.0 * phase / kRate));
			HostAudioPlay(played, kPiece);
		}
		vTaskDelay(pdMS_TO_TICKS(10));
		// (all that has come: taken a piece at a time this would fall behind,
		// and measure what was heard before)
		for (long got; (got = gHeard.Filled()) > 0; )
		{
			if (got > kPiece)
				got = kPiece;
			HostMicrophoneRecord(heard, got);
			// (the first 100 ms are what the sound's start takes to arrive)
			if (t >= 100)
			{
				if (started == 0)
					started = esp_timer_get_time();
				for (long i = 0; i < got; i++)
				{
					double x = heard[i];
					sum += x;
					squares += x * x;
					double q0 = kCoefficient * q1 - q2 + x;
					q2 = q1;
					q1 = q0;
				}
				samples += got;
			}
		}
	}
	Heard result = { 0, 0, 0, 0 };
	if (samples != 0)
	{
		result.rate = samples / ((esp_timer_get_time() - started) / 1000000.0);
		result.mean = sum / samples;
		result.level = sqrt(fabs(squares / samples - result.mean * result.mean));
		result.tone = sqrt(fabs(q1 * q1 + q2 * q2 - kCoefficient * q1 * q2)) * 2 / samples;
	}
	return result;
}


static void
Report(const char* what, const Heard& heard)
{
	printf("newton: %s: mean %.0f, level %.0f, at 1 kHz %.0f, %.0f samples a second\n",
		   what, heard.mean, heard.level, heard.tone, heard.rate);
}


// The console's !sound: the path measured with nobody listening.  With the
// codec's own loop (REG44: the DAC's data sent back as the ADC's left
// channel) the tone should come back at 8000 at 21600 samples a second,
// which proves the I2S and both data pins; then the loudspeaker heard through
// the microphone, which proves the rest - well above what the room gives.
void
HostDeviceAudioTest(void)
{
	if (!Started())
	{
		printf("newton: no sound on this board\n");
		return;
	}
	long none = 0;
	HostMicrophoneRecord((short*) &none, 0);		// (the microphone started)
	vTaskDelay(pdMS_TO_TICKS(100));
	Heard settling = Listen(1000, false);			// (and its filter settled)
	Report("settling", settling);
	Report("the room", Listen(500, false));
	CodecWrite(0x44, 0x48);
	vTaskDelay(pdMS_TO_TICKS(50));
	Report("the codec's loop", Listen(500, true));
	CodecWrite(0x44, 0x08);
	vTaskDelay(pdMS_TO_TICKS(100));
	Report("the loudspeaker", Listen(800, true));
}
