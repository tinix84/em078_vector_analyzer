/*
 * video.c
 *
 *  Created on: 26 ���. 2018 �.
 *      Author: user
 */

#include "../include/video.h"

uint8_t red;
uint8_t green;
uint8_t blue;

volatile uint16_t buffer[1024 * 768 * 4 + 4];
volatile uint16_t screen[1024 * 768 * 4 + 4];
volatile uint16_t splash[1024 * 768 * 4 + 4];

volatile uint8_t font[256][64];

void setcolor(uint8_t r, uint8_t g, uint8_t b) {
	red = r;
	green = g;
	blue = b;
}
void drawpixel(int32_t x, int32_t y) {

	if (x>=0 && y>=0 && x < WIDTH && y < HEIGHT) {
		buffer[(y * 1024 + x) * 4 + 1] = red;
		buffer[(y * 1024 + x) * 4 + 0] = blue + green * 256;
	}
}

void drawline(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
	uint16_t steps;
	int cc;

	if (x1>=0 && x2 >=0 && y1>=0 && y2>=0 && x1<=WIDTH && x2<=WIDTH && y1<= HEIGHT && y2<=HEIGHT){

	if (abs(x2 - x1) > abs(y2 - y1))
		steps = abs(x2 - x1);
	else
		steps = abs(y2 - y1);

	for (cc = 0; cc <= steps; cc++) {
		drawpixel(x1 + floor(1.0 * (x2 - x1) / steps * cc),
				y1 + floor(1.0 * (y2 - y1) / steps * cc));
	}
	}
}

double hue2rgb(double p, double q, double tt) {
	double t;
	t = tt;
	if (t < 0)
		t += 1;
	if (t > 1)
		t -= 1;

	if (t < 1.0 / 6.0)
		return p + (q - p) * 6 * t;
	else if (t < 1.0 / 2.0)
		return q;
	else if (t < 2.0 / 3.0)
		return p + (q - p) * (2.0 / 3.0 - t) * 6;
	else
		return p;
}

uint8_t hslToR(double h, double s, double l) {

	if (s == 0) {
		return l;
	} else {

		double q = (l < 0.5) ? (l * (1 + s)) : (l + s - l * s);
		double p = 2 * l - q;
		return floor(255.0 * hue2rgb(p, q, h + 1.0 / 3.0));

	}
}
///

uint8_t hslToG(double h, double s, double l) {

	if (s == 0) {
		return l;
	} else {

		double q = (l < 0.5) ? (l * (1 + s)) : (l + s - l * s);
		double p = 2 * l - q;

		return floor(255.0 * hue2rgb(p, q, h));

	}
}
///
uint8_t hslToB(double h, double s, double l) {

	if (s == 0) {
		return l;
	} else {

		double q = (l < 0.5) ? (l * (1 + s)) : (l + s - l * s);
		double p = 2 * l - q;

		return floor(255.0 * hue2rgb(p, q, h - 1.0 / 3.0));
	}
}

void drawtext(const char * string, int size, uint16_t x, uint16_t y) {
	int counter;
	int i;
	int j;

	for (counter = 0; counter < size; counter++) {
		for (i = 0; i < 8; i++) {
			for (j = 0; j < 8; j++) {
				if (font[string[counter]][j * 8 + i] == 0)
					drawpixel(x + counter * 8 + i, y + j);
			}
		}
	}
}

void clrscr(void) {

	memcpy((void *) buffer, (void *) splash, 1024 * 768 * 4 * sizeof(uint16_t));

}

void videoinit(void) {
	FIL Fil;
	uint8_t header[54];
	UINT bytes_read;
	int io_x = 0;
	int io_y = 0;
	int io_sub_x;
	int i, j;
	volatile uint8_t io_buff[128 * 3];
	//ALT_STATUS_CODE ALT_RESULT;
	uint8_t fontimage[256 * 128];
	int char_x;
	int char_y;

	red = 255;
	green = 255;
	blue = 255;

	memset((void *) splash, 0, 1024 * 768 * 4 * sizeof(uint16_t));

//	if (f_open(&Fil, "splash.bmp", FA_READ) == FR_OK) {
//		f_read(&Fil, header, 54, &bytes_read);
//		for (io_y = 0; io_y < 768; io_y++)
//			for (io_x = 0; io_x < 8; io_x++) //8 = 1024/128 - number of times buffer fit into line
//					{
//				f_read(&Fil, (void *)io_buff, 128 * 3, &bytes_read);
//
//				for (io_sub_x = 0; io_sub_x < 128; io_sub_x++) {
//					splash[((767 - io_y) * 1024 + io_x * 128 + io_sub_x) * 4 + 1] =
//							io_buff[io_sub_x * 3 + 2];
//					splash[((767 - io_y) * 1024 + io_x * 128 + io_sub_x) * 4 + 0] =
//							io_buff[io_sub_x * 3]
//									+ io_buff[io_sub_x * 3 + 1] * 256;
//				}
//			}
//		f_close(&Fil);
//	}

	memset(font, 1, 256 * 64 * sizeof(uint8_t));
	if (f_open(&Fil, "font.bmp", FA_READ) == FR_OK) {
		f_read(&Fil, header, 54, &bytes_read);

		for (io_y = 0; io_y < 128; io_y++) // read all, but actually we need only the top half
			// io_y from 64 to 127
			for (io_x = 0; io_x < 2; io_x++)//2 = 256/128 - number of times buffer fit into line
					{
				f_read(&Fil, (void *) io_buff, 128 * 3, &bytes_read);
				for (io_sub_x = 0; io_sub_x < 128; io_sub_x++) {
					fontimage[(127 - io_y) * 256 + io_x * 128 + io_sub_x] =
							io_buff[io_sub_x * 3];
				}
			}

		f_close(&Fil);
	}

	for (char_x = 0; char_x < 32; char_x++)
		for (char_y = 0; char_y < 8; char_y++) {
			for (i = 0; i < 8; i++)
				for (j = 0; j < 8; j++)
					font[char_y * 32 + char_x][j * 8 + i] = fontimage[char_x * 8
							+ i + (char_y * 8 + j) * 256];
		}

	memset((void *) screen, 0, 1024 * 768 * 4 * sizeof(uint16_t));
	memset((void *) buffer, 0, 1024 * 768 * 4 * sizeof(uint16_t));
}

void swapbuffers(void) {
	memcpy((void*) screen, (void*) buffer, 1024 * 768 * 4 * sizeof(uint16_t));
}

