// c / cpp
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// hwlib
#include <alt_16550_uart.h>
#include <alt_address_space.h>
#include <alt_bridge_manager.h>
#include <alt_cache.h>
#include <alt_clock_manager.h>
#include <alt_fpga_manager.h>
#include <alt_generalpurpose_io.h>
#include <alt_globaltmr.h>
#include <alt_int_device.h>
#include <alt_interrupt.h>
#include <alt_interrupt_common.h>
#include <alt_timers.h>
#include <alt_mmu.h>
#include <alt_printf.h>
#include <alt_watchdog.h>
#include <hwlib.h>
// socal
#include <alt_gpio.h>
#include <alt_sdmmc.h>
#include <hps.h>
#include <socal.h>
// project
#include "../include/alt_pt.h"
#include "../include/diskio.h"
#include "../include/ff.h"
#include "../include/pio.h"
#include "../include/system.h"
#include "../include/va_sm.h"
#include "../include/video.h"

// int __auto_semihosting;

#define FREQ 950000
#define XSIZE 256
#define YSIZE 192
#define PI 3.141596

#define BUTTON_IRQ ALT_INT_INTERRUPT_F2S_FPGA_IRQ0
#define fpga_leds (void*)((uint32_t)ALT_LWFPGASLVS_ADDR + (uint32_t)LED_PIO_BASE)

ALT_16550_HANDLE_t uart;

#define GPT_TIMER_ID ALT_GPT_OSC1_TMR0
#define GPT_TIMER_CLOCK ALT_CLK_OSC1

#define MARGX 40
#define MARGBOT 40
#define MARGTOP 20

#define MINX1 MARGX
#define MAXX1 (WIDTH-MARGX)
#define MINY1 (HEIGHT-MARGBOT)
#define MAXY1 MARGTOP

#define HORLINES 3
#define VERLINES 5

//#define MAXYVAL1 262144
#define MAXYVAL1 262144
#define MINYVAL1 0
#define MAXYVAL2 (51471)
#define MINYVAL2 (-51471)

//MAXYVAL2 = PI/2.0*32768;

extern volatile uint16_t screen[1024 * 768 * 4 + 4];
double coeff;
extern bool is_screen_refresh_allowed;
// Calculations
int32_t *data_arr;
int32_t data_len;
int32_t data_cnt;
uint32_t freq_low = 1000;
uint32_t freq_high = 1000000;
uint32_t freq_step = 250;

uint32_t freq;
uint32_t timer_prescaler;
alt_freq_t timer_clock;
uint64_t secstart;
uint64_t secend;
uint16_t frames;
uint16_t fps;

//extern volatile uint8_t font[256][64];

ALT_STATUS_CODE delay_us(uint32_t us) {
	ALT_STATUS_CODE status = ALT_E_SUCCESS;

	uint64_t start_time = alt_globaltmr_get64();
	uint32_t timer_prescaler = alt_globaltmr_prescaler_get() + 1;
	uint64_t end_time;
	alt_freq_t timer_clock;

	status = alt_clk_freq_get(ALT_CLK_MPU_PERIPH, &timer_clock);
	end_time = start_time + us * ((timer_clock / timer_prescaler) / 1000000);

	while (alt_globaltmr_get64() < end_time) {
	}

	return status;
}

void fpgaprepare() {
	uintptr_t pa;

	alt_write_word(ALT_LWFPGASLVS_OFST+HDMI_PIO_READY_BASE, 0x0000);
	delay_us(1);

	pa = alt_mmu_va_to_pa((void*) screen, NULL, NULL);

	alt_write_word(ALT_LWFPGASLVS_OFST+HDMI_PIO_BASE, pa / 8);
	alt_write_word(ALT_LWFPGASLVS_OFST+HDMI_PIO_READY_BASE, 0x0001);

}

/* Interrupt service routine for the buttons */
void fpga_pb_isr_callback(uint32_t icciar, void *context) {
	//int ALT_RESULT;

	/* Read the captured edges */
	uint32_t edges = pio_get_edgecapt(ALT_LWFPGASLVS_OFST + BUTTON_PIO_BASE);

	/* Clear the captured edges */
	pio_set_edgecapt(ALT_LWFPGASLVS_OFST + BUTTON_PIO_BASE, edges);

	/* Increase blinking speed if requested */
	if (edges & 0x1) {
		alt_16550_fifo_write_safe(&uart, "INTERRUPT!\n\r", 12, true);
		if (coeff < 10)
			coeff += 1.0;
		else
			coeff = 1.0;
	}

}

void drawgrid(void) {
	char string1[255];
	int i;
	int x1;
	int x2;
	double khz;

	setcolor(220, 220, 200);
	drawline(MARGX, MARGTOP, WIDTH - MARGX, MARGTOP);
	drawline(WIDTH - MARGX, MARGTOP, WIDTH - MARGX, HEIGHT - MARGBOT);
	drawline(WIDTH - MARGX, HEIGHT - MARGBOT, MARGX, HEIGHT - MARGBOT);
	drawline(MARGX, HEIGHT - MARGBOT, MARGX, MARGTOP);

	setcolor(110, 110, 100);
	//horizontal grid
	for (i = 1; i <= HORLINES; i++)
		drawline(MARGX,
		MARGTOP + i * (HEIGHT - MARGTOP - MARGBOT) / (HORLINES + 1),
		WIDTH - MARGX, 20 + i * (HEIGHT - MARGTOP - MARGBOT) / (HORLINES + 1));

	//vertical grid
	for (i = 1; i <= VERLINES; i++)
		drawline(MARGX + i * (WIDTH - 2 * MARGX) / (VERLINES + 1), MARGTOP,
		MARGX + i * (WIDTH - 2 * MARGX) / (VERLINES + 1), HEIGHT - MARGBOT);

	setcolor(100,100,110);
	drawline(MARGX + 1.0*(freq - freq_low)/(freq_high - freq_low)* (WIDTH - 2 * MARGX), MARGTOP,
		MARGX +  1.0*(freq - freq_low)/(freq_high - freq_low)* (WIDTH - 2 * MARGX), HEIGHT - MARGBOT);


	//X-AXIS LEGEND
	setcolor(220, 220, 200);
	memset(string1, 0, 255);
	for (i = 0; i <= VERLINES + 1; i++) {
		khz = (freq_low + i * (freq_high - freq_low) / (VERLINES + 1)) / 1000.0;
		x1 = floor(khz);
		if (khz < 1000)
			alt_sprintf(string1, "%3d KHz", x1);
		else {
			x2 = (x1 % 1000) / 10;
			x1 = floor(khz / 1000.0);
			//sprintf(string1,"%f Mhz",khz/1000);
			alt_sprintf(string1, "%3d.%0d MHz", x1, x2);
		}
		drawtext(string1, strlen(string1),
				MARGX + i * (WIDTH - 2 * MARGX) / (VERLINES + 1)
						- strlen(string1) / 2 * 8, HEIGHT - MARGBOT + 8);
	}

	//LEFT Y-AXIS LEGEND
	setcolor(255, 0, 0);
	drawtext("1", 1, MARGX - 2 - 8,
	MARGTOP + 0 * (HEIGHT - MARGTOP - MARGBOT) / (HORLINES + 1) - 3);
	drawtext("0.75", 4, MARGX - 2 - 8 * 4,
	MARGTOP + 1 * (HEIGHT - MARGTOP - MARGBOT) / (HORLINES + 1) - 3);
	drawtext("0.5", 3, MARGX - 2 - 8 * 3,
	MARGTOP + 2 * (HEIGHT - MARGTOP - MARGBOT) / (HORLINES + 1) - 3);
	drawtext("0.25", 4, MARGX - 2 - 8 * 4,
	MARGTOP + 3 * (HEIGHT - MARGTOP - MARGBOT) / (HORLINES + 1) - 3);
	drawtext("0", 1, MARGBOT - 2 - 8,
	MARGTOP + 4 * (HEIGHT - MARGTOP - MARGBOT) / (HORLINES + 1) - 3);

	//RIGHT Y-AXIS LEGEND
	setcolor(0, 255, 0);
	alt_sprintf(string1, " 90");
	string1[3] = 248;
	drawtext(string1, 4, WIDTH - MARGX + 2,
	MARGTOP + 0 * (HEIGHT - MARGTOP - MARGBOT) / (HORLINES + 1) - 3);

	alt_sprintf(string1, " 45");
	string1[3] = 248;
	drawtext(string1, 4, WIDTH - MARGX + 2,
	MARGTOP + 1 * (HEIGHT - MARGTOP - MARGBOT) / (HORLINES + 1) - 3);

	alt_sprintf(string1, " 0");
	string1[2] = 248;
	drawtext(string1, 3, WIDTH - MARGX + 2,
	MARGTOP + 2 * (HEIGHT - MARGTOP - MARGBOT) / (HORLINES + 1) - 3);

	alt_sprintf(string1, "-45");
	string1[3] = 248;
	drawtext(string1, 4, WIDTH - MARGX + 2,
	MARGTOP + 3 * (HEIGHT - MARGTOP - MARGBOT) / (HORLINES + 1) - 3);

	alt_sprintf(string1, "-90");
	string1[3] = 248;
	drawtext(string1, 4, WIDTH - MARGX + 2,
	MARGTOP + 4 * (HEIGHT - MARGTOP - MARGBOT) / (HORLINES + 1) - 3);
}

void drawgraphs(void) {
	//int32_t i, x1, x2, y1, y2;
	volatile double xx1;
	volatile double xx2;
	volatile double yy1;
	volatile double yy2;
	volatile double ytemp;
	volatile double ytemp2;
	volatile double ytemp3;
	int32_t i;
	// GRAPH 1
	setcolor(255, 0, 0);
	i = 0;
	xx1 = (
	MINX1 + (MAXX1 - MINX1) * (i) / (data_cnt - 1));


	ytemp = (MAXY1 - MINY1);
	ytemp2 = (data_arr[i] - MINYVAL1);
	ytemp3 = ytemp*ytemp2;
	ytemp = ytemp3 / (MAXYVAL1 - MINYVAL1);
	yy1 = MINY1 + ytemp;

	for (i = 2; i <= data_len; i = i + 2) {

		xx2 = (
		MINX1 + (MAXX1 - MINX1) * (i) / (data_len - 1));

		ytemp = (MAXY1 - MINY1) * (data_arr[i] - MINYVAL1);
			ytemp = ytemp / (MAXYVAL1 - MINYVAL1);
		yy2 = MINY1 + ytemp;

		//if (data_arr[i]!=MINYVAL1)
		drawline(xx1, yy1, xx2, yy2);

		xx1 = xx2;
		yy1 = yy2;
	}

	// GRAPH 2
	setcolor(0, 255, 0);
	i = 0;
	xx1 = (
	MINX1 + 1.0*(MAXX1 - MINX1) * i / (data_len - 1));
	ytemp = (MAXY1 - MINY1) * (data_arr[i+1] - MINYVAL2);
		ytemp = ytemp / (MAXYVAL2 - MINYVAL2);
		yy1 = MINY1 + ytemp;	// Start printing
	for (i = 2; i <= data_len; i = i + 2) {

		xx2 = (
		MINX1 +  1.0*(MAXX1 - MINX1) * (i) / (data_len - 1));
		ytemp = (MAXY1 - MINY1) * (data_arr[i+1] - MINYVAL2);
			ytemp = ytemp / (MAXYVAL2 - MINYVAL2);
			yy2 = MINY1 + ytemp;

		//if (data_arr[i+1]!=MINYVAL2)
		drawline(xx1, yy1, xx2, yy2);

		xx1 = xx2;
		yy1 = yy2;
	}

}


void timer_isr_callback(uint32_t icciar, void *context) {

	char fpsstring[5];

	alt_gpt_int_clear_pending(GPT_TIMER_ID);

	//if (is_screen_refresh_allowed)
	{
	clrscr();
	drawgrid();
	drawgraphs();

	memset(fpsstring, 0, 5);
	alt_sprintf(fpsstring, "%dFPS", fps);
	drawtext(fpsstring, strlen(fpsstring), 2, 2);

	swapbuffers();

	secend = alt_globaltmr_get64();
	frames++;

	if ((secend - secstart) > (timer_clock / timer_prescaler)) {
		fps = frames;
		frames = 0;
		secstart = secend;
	}
	}
}


void init(void) {
	ALT_STATUS_CODE ALT_RESULT = ALT_E_SUCCESS;
	ALT_STATUS_CODE ALT_RESULT2 = ALT_E_SUCCESS;
	//ALT_STATUS_CODE status;
	uint32_t gpt_freq;

	ALT_RESULT = alt_globaltmr_init();
	ALT_RESULT2 = alt_bridge_init(ALT_BRIDGE_F2S, NULL, NULL);

	alt_fpga_init();
	if (alt_fpga_state_get() != ALT_FPGA_STATE_USER_MODE) {
		ALT_RESULT = alt_16550_fifo_write_safe(&uart, "FPGA ERROR\n\r", 12,
		true);
//		status = ALT_E_ERROR;
	}

	ALT_RESULT = alt_bridge_init(ALT_BRIDGE_LWH2F, NULL, NULL);
	alt_addr_space_remap(ALT_ADDR_SPACE_MPU_ZERO_AT_BOOTROM,
			ALT_ADDR_SPACE_NONMPU_ZERO_AT_OCRAM, ALT_ADDR_SPACE_H2F_ACCESSIBLE,
			ALT_ADDR_SPACE_LWH2F_ACCESSIBLE);

	alt_int_global_init();
	alt_int_cpu_init();
	alt_pt_init();
	alt_cache_system_enable();

	//	ALT_RESULT = alt_wdog_reset(ALT_WDOG0);
	//	ALT_RESULT = alt_wdog_reset(ALT_WDOG0_INIT);

	alt_int_dist_target_set(BUTTON_IRQ, 0x3);
	alt_int_dist_trigger_set(BUTTON_IRQ, ALT_INT_TRIGGER_EDGE);
	alt_int_dist_enable(BUTTON_IRQ);
	alt_int_isr_register(BUTTON_IRQ, fpga_pb_isr_callback, NULL);

	/* Clear button presses already detected */
	pio_set_edgecapt(ALT_LWFPGASLVS_OFST + BUTTON_PIO_BASE, 0x1);
	/* Enable the button interrupts */
	pio_set_intmask(ALT_LWFPGASLVS_OFST + BUTTON_PIO_BASE, 0x1);

	alt_gpt_mode_set(GPT_TIMER_ID, ALT_GPT_RESTART_MODE_PERIODIC);
	alt_int_dist_target_set(ALT_INT_INTERRUPT_TIMER_OSC1_0_IRQ, 0x3);
	alt_int_dist_trigger_set(ALT_INT_INTERRUPT_TIMER_OSC1_0_IRQ,
			ALT_INT_TRIGGER_AUTODETECT);
	alt_int_dist_enable(ALT_INT_INTERRUPT_TIMER_OSC1_0_IRQ);
	alt_int_isr_register(ALT_INT_INTERRUPT_TIMER_OSC1_0_IRQ, timer_isr_callback,
			NULL);
	alt_gpt_int_enable(GPT_TIMER_ID);

	alt_clk_freq_get(GPT_TIMER_CLOCK, &gpt_freq);
	alt_gpt_tmr_stop(GPT_TIMER_ID);
	alt_gpt_counter_set(GPT_TIMER_ID, gpt_freq / 15);

	alt_int_cpu_enable();
	alt_int_global_enable();

	ALT_RESULT = alt_clk_is_enabled(ALT_CLK_L4_SP);
	if (ALT_RESULT == ALT_E_FALSE)
		ALT_RESULT = alt_clk_clock_enable(ALT_CLK_L4_SP);

	ALT_RESULT = alt_16550_init(ALT_16550_DEVICE_SOCFPGA_UART0, NULL, 0, &uart);
	ALT_RESULT = alt_16550_baudrate_set(&uart, 115200);
	ALT_RESULT = alt_16550_line_config_set(&uart, ALT_16550_DATABITS_8,
			ALT_16550_PARITY_DISABLE, ALT_16550_STOPBITS_1);
	ALT_RESULT = alt_16550_fifo_enable(&uart);
	ALT_RESULT = alt_16550_enable(&uart);
	//ALT_RESULT = alt_gpio_init();
	//ALT_RESULT = alt_gpio_group_config(led_gpio_init, 24);
	ALT_RESULT = alt_16550_fifo_write_safe(&uart, "Program START\n\r", 15,
	true);

	if (ALT_RESULT2 == ALT_E_SUCCESS)
		ALT_RESULT = alt_16550_fifo_write_safe(&uart, "F2S Bridge init!!\n\r",
				19, true);

}

void setup_fpga_leds(void) {
	alt_write_word(fpga_leds, 0x1);
}

void handle_fpga_leds(void) {
	uint32_t leds_mask = alt_read_word(fpga_leds);
	if (leds_mask != (0x01 << (LED_PIO_DATA_WIDTH - 1))) {
		leds_mask <<= 1;
	} else {
		leds_mask = 0x1;
	}
	alt_write_word(fpga_leds, leds_mask);
}

int main(void) {

	// ???

	double hue = 0;
	FATFS *fs;
	init();

	fs = malloc(sizeof(FATFS));
	f_mount(fs, "0:", 0);

	videoinit();
	fpgaprepare();

	//alt_write_word(ALT_LWFPGASLVS_OFST+LED_PIO_BASE,0xAA);

	timer_prescaler = alt_globaltmr_prescaler_get() + 1;
	alt_clk_freq_get(ALT_CLK_MPU_PERIPH, &timer_clock);
	alt_16550_fifo_write_safe(&uart, "Ready to go!\n\r", 14, true);

	frames = 0;
	fps = 0;

	secstart = alt_globaltmr_get64();

	setup_fpga_leds();
	va_sm_init();

	data_len = ((freq_high - freq_low) / freq_step);
	data_len = 2 * (data_len + 1);
	data_arr = (int32_t*) malloc(data_len * sizeof(int32_t));
	memset(data_arr,0,data_len*sizeof(int32_t));





	alt_gpt_tmr_start(GPT_TIMER_ID);


	for (;;) {
		//main loop
		// -----------------------------------------------------------------------
		// Start calculations

		data_cnt = 0;
		for (freq = freq_low; freq <= freq_high; freq = freq + freq_step) {


			va_nco_meas(data_arr + data_cnt, freq, 640000);
			data_cnt = data_cnt + 2;

		}
		data_cnt--;


		//	free(data_arr);

		// Indication
		handle_fpga_leds();

		setcolor(hslToR(hue, 0.6, 0.5), hslToG(hue, 0.6, 0.5),
				hslToB(hue, 0.6, 0.5));

	}

	// virtually never
	/*
	 alt_int_global_uninit	();
	 alt_bridge_uninit(ALT_BRIDGE_F2S, NULL, NULL);
	 alt_bridge_uninit(ALT_BRIDGE_LWH2F, NULL, NULL);
	 alt_16550_uninit(&uart);
	 */

}
