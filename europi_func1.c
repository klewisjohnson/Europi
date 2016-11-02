// Copyright 2016 Richard R. Goodwin / Audio Morphology
//
// Author: Richard R. Goodwin (richard.goodwin@morphology.co.uk)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.

#include <linux/input.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <linux/kd.h>
#include <pigpio.h>
#include <signal.h>

#include "europi.h"
#include "splash.c"
#include "front_panel.c"
#include "touch.h"
#include "touch.c"
#include "quantizer_scales.h"

//extern struct europi;
extern struct fb_var_screeninfo vinfo;
extern struct fb_fix_screeninfo finfo;
extern struct fb_var_screeninfo orig_vinfo;

extern unsigned hw_version;
extern int fbfd;
extern char *fbp;
extern char *kbfds;
extern int kbfd;
extern int prog_running;
extern int run_stop;
extern int is_europi;
extern int retrig_counter;
extern int extclk_counter;
extern int extclk_level;
extern int clock_counter;
extern int clock_level;
extern int clock_freq;
extern int clock_source;
extern uint8_t PCF8574_state;
extern int led_on;
extern int print_messages;
//extern unsigned int sequence[6][32][3];
extern int current_step;
extern int last_step;
extern int selected_step;
extern int step_one;
extern int step_one_state;
extern uint32_t step_tick;
extern uint32_t step_ticks;
extern uint32_t slew_interval;
/* globals used by touchscreen interface */
extern int  xres,yres,x;
extern int Xsamples[20];
extern int Ysamples[20];
extern int screenXmax, screenXmin;
extern int screenYmax, screenYmin;
extern float scaleXvalue, scaleYvalue;
extern int rawX, rawY, rawPressure, scaledX, scaledY;
extern int Xaverage;
extern int Yaverage;
extern int sample;
extern int touched;
extern int encoder_level_A;
extern int encoder_level_B;
extern int encoder_lastGpio;
extern int encoder_pos;
extern int encoder_vel;
extern uint32_t encoder_tick;
extern enum encoder_focus_t encoder_focus;
extern struct europi Europi;
extern pthread_attr_t detached_attr;		
extern int test_v;
pthread_t slewThreadId; 		// Slew thread id


/* Internal Clock
 * 
 * This is the main timing loop for the 
 * whole programme, and which runs continuously
 * from program start to end.
 * 
 * The Master Clock runs at 96 * The BPM step
 * frequency
 */
void master_clock(int gpio, int level, uint32_t tick)
{
	if ((run_stop == RUN) && (clock_source == INT_CLK)) {
		if (clock_counter++ > 95) clock_counter = 0;
		retrigger(clock_counter);
	}
}

/* External clock - triggered by rising edge of External Clock input
 * If we are clocking from external clock, then it is this function
 * that advances the sequence to the next step
 */
void external_clock(int gpio, int level, uint32_t tick)
{
	if ((run_stop == RUN) && (clock_source == EXT_CLK)) {
		if (clock_counter++ > 95) clock_counter = 0;
		retrigger(clock_counter);
	}
}
/*
 * RETRIGGER
 * 
 * called from both the master and external clocks and
 * moves the sequence on to the next step, forces re-triggers 
 * of any repeating gates.
 * 
 * Both the Master and External clock callbacks call this function
 * depending on the clock source, and both maintain the same 96-step
 * counter.
 */
void retrigger(int counter){
	int track;
	switch(counter) {
		case 0:
			/* start of a new cycle */
			next_step();
			/* IF we've got Europi hardware, set the Clock output */
			if (is_europi == TRUE){
				/* Track 0 Channel 1 will have the GPIO Handle for the PCF8574 channel 2 is Clock Out*/
				GATESingleOutput(Europi.tracks[0].channels[GATE_OUT].i2c_handle,CLOCK_OUT,DEV_PCF8574,HIGH);
			}
			break;
		case 21:
			/* Re-trig 4 LOW */
			for (track = 0;track < MAX_TRACKS; track++){
				if ((Europi.tracks[track].channels[GATE_OUT].enabled == TRUE ) 
					&&  (Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].steps[current_step].gate_value == 1)
					&& (Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].steps[current_step].retrigger == 4))
					{
					GATESingleOutput(Europi.tracks[track].channels[GATE_OUT].i2c_handle, Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].i2c_device,LOW);
				}
			}
			break;
		case 24:
			/* Re-trig 4 High */
			for (track = 0;track < MAX_TRACKS; track++){
				if ((Europi.tracks[track].channels[GATE_OUT].enabled == TRUE ) 
					&&  (Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].steps[current_step].gate_value == 1)
					&& (Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].steps[current_step].retrigger == 4))
					{
					GATESingleOutput(Europi.tracks[track].channels[GATE_OUT].i2c_handle, Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].i2c_device,HIGH);
				}
			}
			break;
		case 29:
			/* Re-trig 3 Low */
			for (track = 0;track < MAX_TRACKS; track++){
				if ((Europi.tracks[track].channels[GATE_OUT].enabled == TRUE ) 
					&&  (Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].steps[current_step].gate_value == 1)
					&& (Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].steps[current_step].retrigger == 3))
					{
					GATESingleOutput(Europi.tracks[track].channels[GATE_OUT].i2c_handle, Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].i2c_device,LOW);
				}
			}
			break;
		case 32:
			/* Re-trig 3 High */
			for (track = 0;track < MAX_TRACKS; track++){
				if ((Europi.tracks[track].channels[GATE_OUT].enabled == TRUE ) 
					&&  (Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].steps[current_step].gate_value == 1)
					&& (Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].steps[current_step].retrigger == 3))
					{
					GATESingleOutput(Europi.tracks[track].channels[GATE_OUT].i2c_handle, Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].i2c_device,HIGH);
				}
			}
			break;
		case 45:
			/* Re-trig 2 & 4 Lows */
			for (track = 0;track < MAX_TRACKS; track++){
				if ((Europi.tracks[track].channels[GATE_OUT].enabled == TRUE ) 
					&&  (Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].steps[current_step].gate_value == 1)
					&& ((Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].steps[current_step].retrigger == 2)
					|| (Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].steps[current_step].retrigger == 4)))
					{
					GATESingleOutput(Europi.tracks[track].channels[GATE_OUT].i2c_handle, Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].i2c_device,LOW);
				}
			}
			break;
		case 48:
			/* IF we've got Europi hardware, set the Clock output, and end the Step1 pulse if required */
			if (is_europi == TRUE){
				/* Track 0 Channel 1 will have the GPIO Handle for the PCF8574 channel 2 is Clock Out*/
				GATESingleOutput(Europi.tracks[0].channels[GATE_OUT].i2c_handle,CLOCK_OUT,DEV_PCF8574,LOW);
				if(step_one_state == HIGH){
					/* also need to end the Step 1 pulse */
					GATESingleOutput(Europi.tracks[0].channels[GATE_OUT].i2c_handle,STEP1_OUT,DEV_PCF8574,LOW);
					step_one_state = LOW;
					}
			}
			/* Re-trig 2 & 4 High */
			for (track = 0;track < MAX_TRACKS; track++){
				if ((Europi.tracks[track].channels[GATE_OUT].enabled == TRUE ) 
					&&  (Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].steps[current_step].gate_value == 1)
					&& ((Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].steps[current_step].retrigger == 2)
					|| (Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].steps[current_step].retrigger == 4)))
					{
					GATESingleOutput(Europi.tracks[track].channels[GATE_OUT].i2c_handle, Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].i2c_device,HIGH);
				}
			}
			break;
		case 61:
			/* Re-trig 3 Low */
			for (track = 0;track < MAX_TRACKS; track++){
				if ((Europi.tracks[track].channels[GATE_OUT].enabled == TRUE ) 
					&&  (Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].steps[current_step].gate_value == 1)
					&& (Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].steps[current_step].retrigger == 3))
					{ 
					GATESingleOutput(Europi.tracks[track].channels[GATE_OUT].i2c_handle, Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].i2c_device,LOW);
				}
			}
			break;
		case 64:
			/* Re-trig 3 High */
			for (track = 0;track < MAX_TRACKS; track++){
				if ((Europi.tracks[track].channels[GATE_OUT].enabled == TRUE ) 
					&&  (Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].steps[current_step].gate_value == 1)
					&& (Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].steps[current_step].retrigger == 3))
					{
					GATESingleOutput(Europi.tracks[track].channels[GATE_OUT].i2c_handle, Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].i2c_device,HIGH);
				}
			}
			break;
		case 69:
			/* Re-trig 4 Low */
			for (track = 0;track < MAX_TRACKS; track++){
				if ((Europi.tracks[track].channels[GATE_OUT].enabled == TRUE ) 
					&&  (Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].steps[current_step].gate_value == 1)
					&& (Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].steps[current_step].retrigger == 4))
					{
					GATESingleOutput(Europi.tracks[track].channels[GATE_OUT].i2c_handle, Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].i2c_device,LOW);
				}
			}
			break;
		case 72:
			/* Re-trig 4 High */
			for (track = 0;track < MAX_TRACKS; track++){
				if ((Europi.tracks[track].channels[GATE_OUT].enabled == TRUE ) 
					&&  (Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].steps[current_step].gate_value == 1)
					&& (Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].steps[current_step].retrigger == 4))
					{
					GATESingleOutput(Europi.tracks[track].channels[GATE_OUT].i2c_handle, Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].i2c_device,HIGH);
				}
			}
			break;
		case 92:
			/* Gate OFF for all channels unless tied */
			for (track = 0;track < MAX_TRACKS; track++){
				if (Europi.tracks[track].channels[GATE_OUT].enabled == TRUE ) 
					{
					//log_msg("handle: %d, channel: %d, Device: %d, Value: %d\n",Europi.tracks[track].channels[GATE_OUT].i2c_handle, Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].i2c_device,LOW);

					GATESingleOutput(Europi.tracks[track].channels[GATE_OUT].i2c_handle, Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].i2c_device,LOW);
					//GATESingleOutput(3, Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].i2c_device,LOW);
				}
			}
			break;
	}		
}
/* run/stop switch input
 * called whenever the switch changes state
 */
void runstop_input(int gpio, int level, uint32_t tick)
{
	run_stop = level;
}
/* int/ext clock source switch input
 * called whenever the switch changes state
 */
void clocksource_input(int gpio, int level, uint32_t tick)
{
	clock_source = level;
}
/* Reset input
 * Rising edge causes next step to be Step 1
 */
void reset_input(int gpio, int level, uint32_t tick)
{
	if (level == 1)	step_one = TRUE;
}
/* Function called to advance the sequence on to the next step */
void next_step(void)
{
	/* Note the time and length of the previous step
	 * this gives us a running approximation of how
	 * fast the sequence is running, and can be used to 
	 * prevent slews overrunning too badly, time triggers
	 * etc.
	 */
	 uint32_t current_tick = gpioTick();
	step_ticks = current_tick - step_tick;
	step_tick = current_tick;
	//log_msg("Next Step\n");
	int previous_step, channel, track;
	previous_step = current_step;
	if (++current_step >= last_step || step_one == TRUE){
		current_step = 0;
		step_one = FALSE;
		/* IF we've got Europi hardware, set the Clock output */
		if (is_europi == TRUE){
			/* Track 0 Channel 1 will have the GPIO Handle for the PCF8574 channel 3 is Step 1 Out*/
			GATESingleOutput(Europi.tracks[0].channels[GATE_OUT].i2c_handle,STEP1_OUT,DEV_PCF8574,HIGH);
			step_one_state = HIGH;
		}
	}
	/* look for something to do */
	for (track = 0;track < MAX_TRACKS; track++){
		/* set the CV for each channel, BUT ONLY if slew is OFF */
		if ((Europi.tracks[track].channels[CV_OUT].enabled == TRUE ) && (Europi.tracks[track].channels[CV_OUT].steps[current_step].slew_type == Off)){
			DACSingleChannelWrite(Europi.tracks[track].channels[CV_OUT].i2c_handle, Europi.tracks[track].channels[CV_OUT].i2c_address, Europi.tracks[track].channels[CV_OUT].i2c_channel, Europi.tracks[track].channels[CV_OUT].steps[current_step].scaled_value);
		}
		/* set the Gate State for each channel */
		if (Europi.tracks[track].channels[GATE_OUT].enabled == TRUE ){
			GATESingleOutput(Europi.tracks[track].channels[GATE_OUT].i2c_handle, Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].i2c_device,Europi.tracks[track].channels[GATE_OUT].steps[current_step].gate_value);
		}
		/* Is there a Slew set on this step */
		if (Europi.tracks[track].channels[CV_OUT].steps[current_step].slew_type != Off ){
			// launch a thread to handle the slew for this Channel / Step
			// Note that this is created as a Detached, rather than Joinable
			// thread, because otherwise the memory consumed by the thread
			// won't be recovered when the thread exits, leading to memory
			// leaks
			struct slew Slew;
			Slew.i2c_handle = Europi.tracks[track].channels[CV_OUT].i2c_handle;
			Slew.i2c_address = Europi.tracks[track].channels[CV_OUT].i2c_address;
			Slew.i2c_channel = Europi.tracks[track].channels[CV_OUT].i2c_channel;
			Slew.start_value = Europi.tracks[track].channels[CV_OUT].steps[previous_step].scaled_value;
			Slew.end_value = Europi.tracks[track].channels[CV_OUT].steps[current_step].scaled_value;
			Slew.slew_length = Europi.tracks[track].channels[CV_OUT].steps[current_step].slew_length;
			Slew.slew_type = Europi.tracks[track].channels[CV_OUT].steps[current_step].slew_type;
			Slew.slew_shape = Europi.tracks[track].channels[CV_OUT].steps[current_step].slew_shape;
			struct slew *pSlew = malloc(sizeof(struct slew));
			memcpy(pSlew, &Slew, sizeof(struct slew));
			//pthread_attr_t attr;
			//int rc = pthread_attr_init(&attr);
			//rc = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
			if(pthread_create(&slewThreadId, &detached_attr, &SlewThread, pSlew)){
				log_msg("Slew thread creation error\n");
			}
		}
	}


	/* turn off previous step and turn on current step */
	draw_button_number(previous_step, HEX2565(BTN_BACK_CLR));
	draw_button_number(current_step, HEX2565(BTN_STEP_CLR));
	/* if current or previous step is selected, then re-select it */
	if ((previous_step == selected_step) || (current_step == selected_step)){
		select_button(selected_step, HEX2565(BTN_SELECT_CLR));
		}
}

/*
 * Slew Thread - launched for each Track / Step that
 * has a slew value other than SLEW_OFF. This thread
 * function lives just to perform the slew, then
 * ends itself
 */
static void *SlewThread(void *arg)
{
	struct slew *pSlew = (struct slew *)arg;
	uint16_t this_value = pSlew->start_value;
	uint32_t start_tick = gpioTick();
	int step_size;
	if (pSlew->slew_length == 0) {
		// No slew length set, so just output this step and close the thread
		DACSingleChannelWrite(pSlew->i2c_handle, pSlew->i2c_address, pSlew->i2c_channel, pSlew->end_value);
		free(pSlew);
		return(0);
	}
	
	if ((pSlew->end_value > pSlew->start_value) && ((pSlew->slew_shape == Rising) || (pSlew->slew_shape == Both))) {
		// Glide UP
		step_size = (pSlew->end_value - pSlew->start_value) / (pSlew->slew_length / slew_interval);
		if (step_size == 0) step_size = 1;
		//log_msg("Step size up: %d\n",step_size);
		while ((this_value <= pSlew->end_value) && (this_value < (60000 - step_size))){
			DACSingleChannelWrite(pSlew->i2c_handle, pSlew->i2c_address, pSlew->i2c_channel, this_value);
			this_value += step_size;
			usleep(slew_interval / 2);		
			if ((gpioTick() - start_tick) > step_ticks) break;	//We've been here too long
		}
	}
	else if ((pSlew->end_value < pSlew->start_value) && ((pSlew->slew_shape == Falling) || (pSlew->slew_shape == Both))){
		// Glide Down
		step_size = (pSlew->start_value - pSlew->end_value) / (pSlew->slew_length / slew_interval);
		if (step_size == 0) step_size = 1;		
		//log_msg("Step size Down: %d\n",step_size);
		while ((this_value >= pSlew->end_value) && (this_value >= step_size)){
			DACSingleChannelWrite(pSlew->i2c_handle, pSlew->i2c_address, pSlew->i2c_channel, this_value);
			this_value -= step_size;
			usleep(slew_interval / 2);
			if ((gpioTick() - start_tick) > step_ticks) break;	//We've been here too long
		} 
	}
	else {
		// Slew set, but Rising or Falling are off, so just output the end value
		DACSingleChannelWrite(pSlew->i2c_handle, pSlew->i2c_address, pSlew->i2c_channel, pSlew->end_value);
	}
    free(pSlew);
	return(0);
}


/* Function to clear and recreate the
 * main front screen. Should be capable
 * of being called at any time, \	r current
 * state, though its main use is as the  v
 * prog launches to set out the display
 * area
 */
int paint_main(void)
{
	int channel;
	/* splash screen is a full 320x240 image, so will clear the screen */
	splash_screen();
	sleep(2);
	paint_front_panel();
	step_button_grid();
	/* Set all the gate & level indicators to off */
	for(channel=0;channel<CHANNELS;channel++){
		gate_state(channel,0);
		channel_level(channel,0);
	}
}


/*
 * splash_screen - reads the splash image in splash.c
 * and displays it on the screen pixel by pixel. There
 * are probably better ways of doing this, but it works.
 */
void splash_screen(void)
{
	int i,j;
	for(i=0; i<splash_img.height; i++) {
    for(j=0; j<splash_img.width; j++) {
	int r = splash_img.pixel_data[(i*splash_img.width + j)*3 + 0];
	int g = splash_img.pixel_data[(i*splash_img.width + j)*3 + 1];
	int b = splash_img.pixel_data[(i*splash_img.width + j)*3 + 2];
	put_pixel_RGB565(fbp, j, i, RGB2565(r,g,b));
	  
    }
  }
}

/*
 * Front Panel - reads the Front Panel image in splash.c
 * and displays it on the screen
 */
void paint_front_panel(void)
{
	
	int i,j;
	for(i=0; i<front_panel.height; i++) {
    for(j=0; j<front_panel.width; j++) {
      int r = front_panel.pixel_data[(i*front_panel.width + j)*3 + 0];
	  int g = front_panel.pixel_data[(i*front_panel.width + j)*3 + 1];
	  int b = front_panel.pixel_data[(i*front_panel.width + j)*3 + 2];
	  put_pixel_RGB565(fbp, j, i, RGB2565(r,g,b));
	  
    }
  }
}
/*
 * Draws the complete grid of Step buttons.
 * absolute co-ordinates of the Top Left
 * corner of each button are calculated once
 * on startup and stored in the sequence array
 */
void step_button_grid(void)
{
int row, col, x, y;
for (row=0;row<BTN_STEP_ROWS;row++){
	for(col=0;col<BTN_STEP_COLS;col++){
		x = BTN_STEP_TLX + (col * (BTN_STEP_WIDTH + BTN_STEP_HGAP));
		y = BTN_STEP_TLY + (row * (BTN_STEP_HEIGHT + BTN_STEP_VGAP));
		draw_button(x,y,HEX2565(BTN_BACK_CLR));
	}
}
	
}
/* Draws a button, given just a number and a colour */
void draw_button_number(int button_number, unsigned short c)
{
	int row, col, x, y;
	row = (int)(button_number / BTN_STEP_COLS);
	col = button_number % BTN_STEP_COLS;
	x = BTN_STEP_TLX + (col * (BTN_STEP_WIDTH + BTN_STEP_HGAP));
	y = BTN_STEP_TLY + (row * (BTN_STEP_HEIGHT + BTN_STEP_VGAP));
	draw_button(x,y,c);
}
/*
 * draws a sequencer grid button
 * is passed the coordinates of the 
 * Top Left corner
 */
void draw_button(int x, int y, unsigned short c)
{
fill_rect_RGB565(fbp, x, y, BTN_STEP_WIDTH, BTN_STEP_HEIGHT, c);	
}
/* Draws a box around a button to indicate its selected state*/
void select_button(int button_number, unsigned short c)
{
	int row, col, x, y;
	row = (int)(button_number / BTN_STEP_COLS);
	col = button_number % BTN_STEP_COLS;
	x = BTN_STEP_TLX + (col * (BTN_STEP_WIDTH + BTN_STEP_HGAP));
	y = BTN_STEP_TLY + (row * (BTN_STEP_HEIGHT + BTN_STEP_VGAP));
	draw_rect_RGB565(fbp, x,y, BTN_STEP_WIDTH, BTN_STEP_HEIGHT,c);
	draw_rect_RGB565(fbp, x+1,y+1, BTN_STEP_WIDTH-2, BTN_STEP_HEIGHT-2,c);
	draw_rect_RGB565(fbp, x+2,y+2, BTN_STEP_WIDTH-4, BTN_STEP_HEIGHT-4,c);
}
/*
 * channel_levels
 * Draws the current levels 
 * for the Channel level indicators. Full scale
 * is 4096, so we are drawing two rectangles where
 * the height of the lower one is proportional
 * to level/4096
 */
void channel_level(int channel,int level)
{
	int x, y, illuminated_height;
	//DACSingleChannelWrite(DAChandle, channel,level);
	
	illuminated_height = (int)(((float)level/4096)*CHNL_LEVEL_HEIGHT);
	x = CHNL_LEVEL_TLX + (channel * (CHNL_LEVEL_WIDTH+CHNL_LEVEL_HGAP));
	/* Draw the un-lit portion */
	y = CHNL_LEVEL_TLY;
	fill_rect_RGB565(fbp, x, y, CHNL_LEVEL_WIDTH, CHNL_LEVEL_HEIGHT-illuminated_height, HEX2565(CHNL_BACK_CLR));	
//	/* Draw the illuminated portion */
	y = CHNL_LEVEL_TLY + CHNL_LEVEL_HEIGHT - illuminated_height;
	fill_rect_RGB565(fbp, x, y, CHNL_LEVEL_WIDTH, illuminated_height, HEX2565(CHNL_FORE_CLR));	
	
}
/*
 * gate_states
 * Draws the current state for 
 * for the Gate State indicator
 */
void gate_state(int channel, int state)
{
	int x, y;
	/* Temp: just output the state of channel 0 to all bits */
	if (channel == 0){
		//log_msg("Gate state\n");
		//if (state == 1)	i2cWriteWordData(GPIOhandle, 0x09, (unsigned)(0xff)); else i2cWriteWordData(GPIOhandle, 0x09, (unsigned)(0xff==0)); 	
	}
	y = GATE_STATE_TLY;
		x = GATE_STATE_TLX + (channel * (GATE_STATE_WIDTH+GATE_STATE_HGAP));
		if (state == 1){
			fill_rect_RGB565(fbp, x, y, GATE_STATE_WIDTH, GATE_STATE_HEIGHT, HEX2565(GATE_FORE_CLR));	
		}
		else {
			fill_rect_RGB565(fbp, x, y, GATE_STATE_WIDTH, GATE_STATE_HEIGHT, HEX2565(GATE_BACK_CLR));	
		}
}
/*
 * paint_menu
 * Function draws the Menu panel, including the current
 * state of any displayed controls / texst etc., so it
 * can be called after any menu information has been 
 * updated.
 */
void paint_menu(void){
	fill_rect_RGB565(fbp, MENU_X, MENU_Y, MENU_WIDTH, MENU_HEIGHT, MENU_BACK_CLR);	
}
/*
 * button_touched
 * Works out whether the passed coordinates fall within the 
 * boundaries of a sequencer button and, if so, selects it
 */
void button_touched(int x, int y){
	int button_number, tlx, tly, brx, bry, row, col;
	for (button_number = 0; button_number < 32; button_number++){
		row = (int)(button_number / BTN_STEP_COLS);
		col = button_number % BTN_STEP_COLS;
		tlx = BTN_STEP_TLX + (col * (BTN_STEP_WIDTH + BTN_STEP_HGAP));
		tly = BTN_STEP_TLY + (row * (BTN_STEP_HEIGHT + BTN_STEP_VGAP));	
		brx = tlx + BTN_STEP_WIDTH;
		bry = tly + BTN_STEP_HEIGHT;
		/* do our X & Y coordinates fall within this button */
		if ((x >= tlx) && (x <= brx) && (y >= tly) && (y <= bry)){
			/* this button touched */
			/* unselect the previous one (if selected)*/
			if(current_step == selected_step) draw_button_number(selected_step, HEX2565(BTN_STEP_CLR));
			else if(selected_step >= 0) draw_button_number(selected_step, HEX2565(BTN_BACK_CLR));
			if (button_number != selected_step) {
				select_button(button_number, HEX2565(BTN_SELECT_CLR));
				selected_step = button_number;	
			}
			else {
				draw_button_number(selected_step, HEX2565(BTN_BACK_CLR));
				selected_step = -1;
			}
		}
	}
}
 int startup(void)
 {
	 // Initialise the Deatched pThread attribute
	int rc = pthread_attr_init(&detached_attr);
	rc = pthread_attr_setdetachstate(&detached_attr, PTHREAD_CREATE_DETACHED);

	 // Initialise the Europi structure
	int channel;
	for(channel=0;channel < MAX_CHANNELS;channel++){
		
	}
	// Check and note the hardware revision - this is 
	// important because the I2C bus is Bus 0 on the 
	// older boards, and Bus 1 on the later ones
	hw_version = gpioHardwareRevision();
	log_msg("Running on hw_revision: %d\n",hw_version);
	// PIGPIO Function initialisation
	if (gpioInitialise()<0) return 1;
	// TEMP for testing with the K-Sharp screen
	// Use one of the buttons to quit the app
	gpioSetMode(BUTTON1_IN, PI_INPUT);
	gpioGlitchFilter(BUTTON1_IN,100);
	gpioSetPullUpDown(BUTTON1_IN, PI_PUD_UP);
	gpioSetMode(BUTTON2_IN, PI_INPUT);
	gpioGlitchFilter(BUTTON2_IN,100);
	gpioSetPullUpDown(BUTTON2_IN, PI_PUD_UP);
	gpioSetMode(BUTTON3_IN, PI_INPUT);
	gpioGlitchFilter(BUTTON4_IN,100);
	gpioSetPullUpDown(BUTTON3_IN, PI_PUD_UP);
	gpioSetMode(BUTTON4_IN, PI_INPUT);
	gpioGlitchFilter(BUTTON4_IN,100);
	gpioSetPullUpDown(BUTTON4_IN, PI_PUD_UP);
	gpioSetMode(ENCODER_BTN, PI_INPUT);
	gpioGlitchFilter(ENCODER_BTN,100);
	gpioSetPullUpDown(ENCODER_BTN, PI_PUD_UP);

	gpioSetMode(TOUCH_INT, PI_INPUT);
	gpioSetPullUpDown(TOUCH_INT, PI_PUD_UP);
	gpioSetMode(CLOCK_IN, PI_INPUT);
	//gpioGlitchFilter(CLOCK_IN,100);				/* EXT_CLK has to be at the new level for 100uS before it is registered */
	gpioSetMode(RUNSTOP_IN, PI_INPUT);
	gpioSetPullUpDown(RUNSTOP_IN, PI_PUD_UP);
	gpioGlitchFilter(RUNSTOP_IN,100);
	gpioSetMode(INTEXT_IN, PI_INPUT);
	gpioSetPullUpDown(INTEXT_IN, PI_PUD_UP);
	gpioGlitchFilter(INTEXT_IN,100);
	gpioSetMode(RESET_IN, PI_INPUT);
	gpioGlitchFilter(RESET_IN,100);
	// Register callback routines
	gpioSetAlertFunc(BUTTON1_IN, button_1);
	gpioSetAlertFunc(BUTTON2_IN, button_2);
	gpioSetAlertFunc(BUTTON3_IN, button_3);
	gpioSetAlertFunc(BUTTON4_IN, button_4);
	gpioSetAlertFunc(ENCODER_BTN, encoder_button);
	gpioSetAlertFunc(TOUCH_INT, touch_interrupt);
	gpioSetAlertFunc(CLOCK_IN, external_clock);
	gpioSetAlertFunc(RUNSTOP_IN, runstop_input);
	gpioSetAlertFunc(INTEXT_IN, clocksource_input);
	gpioSetAlertFunc(RESET_IN, reset_input);
	/* Establish Rotary Encoder Callbacks */
	gpioSetMode(ENCODERA_IN, PI_INPUT);
	gpioSetMode(ENCODERB_IN, PI_INPUT);
	gpioSetPullUpDown(ENCODERA_IN, PI_PUD_UP);
	gpioSetPullUpDown(ENCODERB_IN, PI_PUD_UP);
	gpioSetAlertFunc(ENCODERA_IN, encoder_callback);
	gpioSetAlertFunc(ENCODERB_IN, encoder_callback);
	encoder_level_A = 0;
	encoder_level_B = 0;
	encoder_tick = gpioTick();
	encoder_focus = none;

	/* Open all the hardware ports */
	hardware_init();

	//initialise the sequence for testing purposes
	init_sequence();

	// Attempt to open the Framebuffer
	// for reading and writing
	fbfd = open("/dev/fb1", O_RDWR);
	if (!fbfd) {
		log_msg("Error: cannot open framebuffer device.");
		return(1);
	}
	unsigned int screensize = 0;
	// Get current screen metrics
	if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo)) {
		log_msg("Error reading variable information.");
	}
	// printf("Original %dx%d, %dbpp\n", vinfo.xres, vinfo.yres, vinfo.bits_per_pixel );
	// Store this for when the prog closes (copy vinfo to vinfo_orig)
	// because we'll need to re-instate all the existing parameters
	memcpy(&orig_vinfo, &vinfo, sizeof(struct fb_var_screeninfo));
  if (ioctl(fbfd, FBIOGET_VSCREENINFO, &orig_vinfo)) {
    log_msg("Error reading variable information.");
  }
  // Change variable info - force 16 bit and resolution
  vinfo.bits_per_pixel = 16;
  vinfo.xres = X_MAX;
  vinfo.yres = Y_MAX;
  vinfo.xres_virtual = vinfo.xres;
  vinfo.yres_virtual = vinfo.yres;
  
  if (ioctl(fbfd, FBIOPUT_VSCREENINFO, &vinfo)) {
    log_msg("Error setting variable information.");
  }
  
  // Get fixed screen information
  if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo)) {
    log_msg("Error reading fixed information.");
  }

  // map fb to user mem 
  screensize = vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8;
  xres = vinfo.xres;
  yres = vinfo.yres;
  fbp = (char*)mmap(0, 
        screensize, 
        PROT_READ | PROT_WRITE, 
        MAP_SHARED, 
        fbfd, 
        0);

  if ((int)fbp == -1) {
    log_msg("Failed to mmap.");
	return -1;
  }
  // Open the touchscreen
  	if (openTouchScreen() == 1)
		log_msg("error opening touch screen"); 
	getTouchScreenDetails(&screenXmin,&screenXmax,&screenYmin,&screenYmax);
	//log_msg("screenXmin: %d, screenXmax: %d, screenYmin:L %d, screenYmax: %d \n",screenXmin,screenXmax,screenYmin,screenYmax);
	scaleXvalue = ((float)screenXmax-screenXmin) / xres;
	//log_msg ("X Scale Factor = %f\n", scaleXvalue);
	scaleYvalue = ((float)screenYmax-screenYmin) / yres;
	//log_msg ("Y Scale Factor = %f\n", scaleYvalue);

  	// Turn the cursor off
    kbfd = open(kbfds, O_WRONLY);
    if (kbfd >= 0) {
        ioctl(kbfd, KDSETMODE, KD_GRAPHICS);
    }
    else {
        log_msg("Could not open kbfds.");
    }
	/* Start the internal sequencer clock */
	run_stop = STOP;		/* master clock is running, but step generator is halted */
	gpioHardwarePWM(MASTER_CLK,clock_freq,500000);
	gpioSetAlertFunc(MASTER_CLK, master_clock);
	
	prog_running = 1;
  return(0);
}

/* Things to do as the prog closess */
int shutdown(void)
 {
	 int track;
	 /* clear down all CV / Gate outputs */
	for (track = 0;track < MAX_TRACKS; track++){
		/* set the CV for each channel to the Zero level*/
		if (Europi.tracks[track].channels[0].enabled == TRUE ){
			DACSingleChannelWrite(Europi.tracks[track].channels[0].i2c_handle, Europi.tracks[track].channels[0].i2c_address, Europi.tracks[track].channels[0].i2c_channel, Europi.tracks[track].channels[0].scale_zero);
		}
		/* set the Gate State for each channel to OFF*/
		if (Europi.tracks[track].channels[GATE_OUT].enabled == TRUE ){
			//GATESingleOutput(Europi.tracks[track].channels[GATE_OUT].i2c_handle, Europi.tracks[track].channels[GATE_OUT].i2c_channel,Europi.tracks[track].channels[GATE_OUT].i2c_device,0x00);
		}
	}
	 /* Clear the screen to black */
	fill_rect_RGB565(fbp, 0, 0, X_MAX - 1, Y_MAX - 1, HEX2565(0x000000));
	/* put up our splash screen (why not!) */
	splash_screen();
	/* unload the PIGPIO library */
	gpioTerminate();
	unsigned int screensize = vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8;
	if (kbfd >= 0) {
		ioctl(kbfd, KDSETMODE, KD_TEXT);
	}
	else {
		log_msg("Could not reset keyboard mode.");
	}
	munmap(fbp, screensize);
	if (ioctl(fbfd, FBIOPUT_VSCREENINFO, &orig_vinfo)) {
		log_msg("Error re-setting variable information.");
	}
	close(fbfd);
	return(0);
 }

/* Deals with messages that need to be logged
 * This could be modified to write them to a 
 * log file. In the first instance, it simply
 * printf's them
 */
void log_msg(const char* format, ...)
{
   char buf[128];
	if (print_messages == TRUE){
	va_list args;
	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	fprintf(stderr, "%s", buf);
	va_end(args);
	}
}

/* function called whenever the Touch Screen detects an input
 * it can be called multiple times determined by the global
 * constant SAMPLE_AMOUNT and, once it has that many samples
 * it averages them to determine where the touch occurred
 * this is a crude way of avoiding jitter
 * */
void touch_interrupt(int gpio, int level, uint32_t tick)
{
	/* maybe what I need to do here is, rather
	 * than read the sample at this point, just set a flag 
	 * to indicate that a sample is ready to be read
	 * then do the actual reading and averaging during
	 * the main program loop??
	 * */
	 touched = 1;
}

/* Called to initiate a controlled shutdown and
 * exit of the prog
 */
void controlled_exit(int gpio, int level, uint32_t tick)
{
	prog_running = 0;
}

void encoder_callback(int gpio, int level, uint32_t tick){
	int dir = 0;
	int vel;
	uint32_t tick_diff;
	if (gpio == ENCODERA_IN) encoder_level_A = level; else encoder_level_B = level;
	if (gpio != encoder_lastGpio)	/* debounce */
	{
		encoder_lastGpio=gpio;
		if((gpio == ENCODERA_IN) && (level==1))
		{
			if (encoder_level_B) dir=1;
		}
		else if ((gpio == ENCODERB_IN) && (level==1))
		{
			if(encoder_level_A) dir = -1;
		}
	}
	if(dir != 0)
	{
		/* work out the speed */
		tick_diff = tick-encoder_tick;
		encoder_tick = tick;
		//log_msg("Ticks: %d\n",tick_diff);
		switch(tick_diff){
				case 0 ... 3000:
					vel = 10;
				break;
				case 3001 ... 5000:
					vel = 9;
				break;
				case 5001 ... 10000:
					vel = 8;
				break;
				case 10001 ... 20000:
					vel = 7;
				break;
				case 20001 ... 30000:
					vel = 6;
				break;
				case 30001 ... 40000:
					vel = 5;
				break;
				case 40001 ... 50000:
					vel = 4;
				break;
				case 50001 ... 100000:
					vel = 3;
				break;
				case 100001 ... 200000:
					vel = 2;
				break;
				default:
					vel = 1;
		}
		/* Call function based on current encoder focus */
		switch(encoder_focus){
		case pitch_cv:
			pitch_adjust(dir, vel);
		break;
//		default: // do nothing 
//		log_msg("Vel: %d, Dir: %d\n",vel,dir);
		}
		switch(encoder_focus){
		case none:
			break;
		case pitch_cv:
			pitch_adjust(dir, vel);
			break;
		case slew_type:
			slew_adjust(dir, vel);
			break;
		case gate_on_off:
			gate_onoff(dir,vel);
			break;
		case repeat:
			step_repeat(dir,vel);
			break;
		case quantise:
			break;
		}
		
	}
}


/* Rotary encoder button pressed */
void encoder_button(int gpio, int level, uint32_t tick)
{
	if(level == 0){
		switch(encoder_focus){
		case none:
			encoder_focus = pitch_cv;
			put_string(fbp, 20, 215, " Adj Pitch:", HEX2565(0x000000), HEX2565(0xFFFFFF));
			break;
		case pitch_cv:
			encoder_focus = slew_type;
			put_string(fbp, 20, 215, "Slew Type:", HEX2565(0x000000), HEX2565(0xFFFFFF));
			break;
		case slew_type:
			encoder_focus = gate_on_off;
			put_string(fbp, 20, 215, "      Gate:", HEX2565(0x000000), HEX2565(0xFFFFFF));
			break;
		case gate_on_off:
			encoder_focus = repeat;
			put_string(fbp, 20, 215, "    Repeat:", HEX2565(0x000000), HEX2565(0xFFFFFF));
			break;
		case repeat:
			encoder_focus = quantise;
			put_string(fbp, 20, 215, "  Quantise:", HEX2565(0x000000), HEX2565(0xFFFFFF));
			break;
		case quantise:
			encoder_focus = none;
			put_string(fbp, 20, 215, "Menu       ", HEX2565(0x000000), HEX2565(0xFFFFFF));
			break;
		default:
			encoder_focus = none;
			put_string(fbp, 20, 215, "Menu       ", HEX2565(0x000000), HEX2565(0xFFFFFF));
		}
		
	}
}

/* Button 1 pressed */
void button_1(int gpio, int level, uint32_t tick)
{
	if (level == 1) {
		// Temp:Controlled shutdown
		prog_running = 0;
	}

}

/* Button 2 pressed */
void button_2(int gpio, int level, uint32_t tick)
{
	float bpm;
	if (level == 1) {
		clock_freq += 10;
		gpioHardwarePWM(MASTER_CLK,clock_freq,500000);
		bpm = ((((float)clock_freq)/48) * 60);
		log_msg("BPM: %f\n",bpm);
	}
}
/* Button 3 pressed */
void button_3(int gpio, int level, uint32_t tick)
{
	float bpm;
	if (level == 1) {
		clock_freq -= 10;
		if (clock_freq < 1) clock_freq = 1;
		gpioHardwarePWM(MASTER_CLK,clock_freq,500000);
		bpm = ((((float)clock_freq)/48) * 60);
		log_msg("BPM: %f\n",bpm);
	}
}
	
/* Button 4 pressed */
void button_4(int gpio, int level, uint32_t tick)
{
	float bpm;
	if (level == 1) {
		clock_freq += 10;
		gpioHardwarePWM(MASTER_CLK,clock_freq,500000);
		bpm = ((((float)clock_freq)/48) * 60);
		log_msg("BPM: %f\n",bpm);
	}
}

/*
 * Specifically looks for a PCF8574 on the default
 * address of 0x20 this should be unique to the Main
 * Europi module, therefore it should be safe to 
 * return a handle to the DAC8574 on the anticipated
 * address of 0x08
 */
int EuropiFinder()
{
	int retval;
	int DACHandle;
	unsigned pcf_addr = PCF_BASE_ADDR;
	unsigned pcf_handle = i2cOpen(1,pcf_addr,0);
	if(pcf_handle < 0)return -1;
	// Unfortunately, this tends to retun a valid handle even though the
	// device doesn't exist. The only way to really check it is there 
	// to try writing to it, which will fail if it doesn't exist.
	retval = i2cWriteByte(pcf_handle, (unsigned)(0xF0));
	// either that worked, or it didn't. Either way we
	// need to close the handle to the device for the
	// time being
	i2cClose(pcf_handle);
	if(retval < 0) return -1;	
	// pass back a handle to the DAC8574, 
	// which will be on i2c Address 0x4C
	DACHandle = i2cOpen(1,DAC_BASE_ADDR,0);
	return DACHandle;
}

/* looks for an MCP23008 on the passed address. If one
 * exists, then it should be safe to assume that this is 
 * a Minion, in which case it is safe to pass back a handle
 * to the DAC8574
 */
int MinionFinder(unsigned address)
{
	int handle;
	int mcp_handle;
	int retval;
	unsigned i2cAddr;
	if((address > 8) || (address < 0)) return -1;
	i2cAddr = MCP_BASE_ADDR | (address & 0x7);
	mcp_handle = i2cOpen(1,i2cAddr,0);
	if (mcp_handle < 0) return -1;
	/* 
	 * we have a valid handle, however whether there is actually
	 * a device on this address can seemingly only be determined 
	 * by attempting to write to it.
	 */
	 retval = i2cWriteWordData(mcp_handle, 0x00, (unsigned)(0x0));
	 // close the handle to the PCF8574 (it will be re-opened shortly)
	 i2cClose(mcp_handle);
	 if(retval < 0) return -1;
	 i2cAddr = DAC_BASE_ADDR | (address & 0x3);
	 handle = i2cOpen(1,i2cAddr,0);
	 return handle;
}

/* 
 * DAC8574 16-Bit DAC single channel write 
 * Channel, in this context is the full 6-Bit address
 * of the channel - ie [A3][A2][A1][A0][C1][C0]
 * [A1][A0] are ignored, because we already have a 
 * handle to a device on that address. 
 * [A3][A2] are significant, as they need to match
 * the state of the address lines on the DAC
 * The ctrl_reg needs to look like this:
 * [A3][A2][0][1][x][C1][C0][0]
 */
void DACSingleChannelWrite(unsigned handle, uint8_t address, uint8_t channel, uint16_t voltage){
	uint16_t v_out;
	uint8_t ctrl_reg;
	//log_msg("%d, %d, %d, %d\n",handle,address,channel,voltage);
	ctrl_reg = (((address & 0xC) << 4) | 0x10) | ((channel << 1) & 0x06);
	//log_msg("handle: %0x, address: %0x, channel: %d, ctrl_reg: %02x, Voltage: %d\n",handle, address, channel,ctrl_reg,voltage);
	//swap MSB & LSB because i2cWriteWordData sends LSB first, but the DAC expects MSB first
	v_out = ((voltage >> 8) & 0x00FF) | ((voltage << 8) & 0xFF00);
	i2cWriteWordData(handle,ctrl_reg,v_out);
}
/* 
 * GATEMultiOutput
 * 
 * Writes the passed 8-Bit value straight to the GPIO Extender
 * the idea being that this is a quicker way of turning all
 * gates off on a Minion at the end of a Gate Pulse
 * 
 * Note that the LS 4 bits are the Gate, and the MS 4 bits are
 * the LEDs, so the lower 4 bits should match the upper 4 bits
 */
void GATEMultiOutput(unsigned handle, uint8_t value)
{
	i2cWriteByteData(handle, 0x09,value);	
}
/*
 * Outputs the passed value to the GATE output identified
 * by the Handle to the Open device, and the channel (0-3)
 * As this is just a single bit, this function has to 
 * read the current state of the other gates on the GPIO
 * extender, incorporate this value, then write everything
 * back.
 * Note that, on the Minion, the Gates are on GPIO Ports 0 to 3, 
 * though the gate indicator LEDs are on GPIO Ports 4 to 7, so
 * the output values from ports 0 to 3 need to be mirrored
 * to ports 4 - 7
 * The i2c Protocols are different for the PCF8574 used on the 
 * Europi, and the MCP23008 used on the Minions, and the MCP23008
 * will drive an LED from its High output, but the PCF8574 will
 * only pull it low!
 */ 
void GATESingleOutput(unsigned handle, uint8_t channel,int Device,int Value)
{
	//log_msg("handle: %d, channel: %d, Device: %d, Value: %d\n",handle,channel,Device,Value);
	uint8_t curr_val;
	if(Device == DEV_MCP23008){
		curr_val = i2cReadByteData(handle, 0x09);
		if (Value > 0){
			// Set corresponding bit high
			curr_val |= (0x01 << channel);
			curr_val |= (0x01 << (channel + 4));
		}
		else {
			// Set corresponding bit low
			curr_val &= ~(0x01 << channel);
			curr_val &= ~(0x01 << (channel + 4));
		}
		i2cWriteByteData(handle, 0x09,curr_val);	
	}
	else if (Device == DEV_PCF8574){
		/* The PCF8574 will only turn an LED on
		 * when its output is LOW, so the upper
		 * 4 bits need to be the inverse of the
		 * lower 4 bits. Also, we cannot use the 
		 * trick of reading the current state of
		 * the port, as it's bi-directonal, but 
		 * non-latching. Therefore we have to keep
		 * the current state in a global variable
		 */
		if (Value > 0){
			// Set corresponding bit high
			PCF8574_state |= (0x01 << channel);
			// the equivalent in the MS Nibble needs to be low to turn the LED on
			PCF8574_state &= ~(0x01 << (channel+4));
		}
		else {
			// Set corresponding bit low
			PCF8574_state &= ~(0x01 << channel);
			// the equivalent in the MS Nibble needs to be high to turn the LED off
			PCF8574_state |= (0x01 << (channel+4));
		}
		i2cWriteByte(handle,PCF8574_state);
	}
}
/*
 * Initialises all the hardware ports - scanning for connected
 * Minions etc.
 */
void hardware_init(void)
{
	unsigned track = 0;
	unsigned address;
	unsigned mcp_addr;
	unsigned pcf_addr;
	uint8_t chnl;
	int handle;
	int gpio_handle;
	int pcf_handle;
	/* Before we start, make sure all Tracks / Channels are disabled */
	for (track = 0; track < MAX_TRACKS;track++){
		Europi.tracks[track].channels[0].enabled = FALSE;
		Europi.tracks[track].channels[1].enabled = FALSE;
	}
	/* 
	 * Specifically look for a PCF8574 on address 0x38
	 * if one exists, then it's on the Europi, so the first
	 * two Tracks will be allocated to the Europi
	 */
	track = 0;
	address = 0x08;
	handle = EuropiFinder();
	if (handle >=0){
		/* we have a Europi - it supports 2 Tracks each with 2 channels (CV + GATE) */
		log_msg("Europi Found on i2cAddress %d handle = %d\n",address, handle);
		is_europi = TRUE;
		/* As this is a Europi, then there should be a PCF8574 GPIO Expander on address 0x38 */
		pcf_addr = PCF_BASE_ADDR;
		pcf_handle = i2cOpen(1,pcf_addr,0);
		if(pcf_handle < 0){log_msg("failed to open PCF8574 associated with DAC8574 on Addr: 0x08");}
		if(pcf_handle >= 0) {
			/* Gates off, LEDs off */
			i2cWriteByte(pcf_handle, (unsigned)(0xF0));
		}
		Europi.tracks[track].channels[0].enabled = TRUE;
		Europi.tracks[track].channels[0].type = CHNL_TYPE_CV;
		Europi.tracks[track].channels[0].quantise = 0;			/* Quantization off by default */
		Europi.tracks[track].channels[0].i2c_handle = handle;			
		Europi.tracks[track].channels[0].i2c_device = DEV_DAC8574;
		Europi.tracks[track].channels[0].i2c_address = 0x08;
		Europi.tracks[track].channels[0].i2c_channel = 0;		
		Europi.tracks[track].channels[0].scale_zero = 280;		/* Value required to generate zero volt output */
		Europi.tracks[track].channels[0].scale_max = 63000;		/* Value required to generate maximum output voltage */
		Europi.tracks[track].channels[0].transpose = 0;			/* fixed (transpose) voltage offset applied to this channel */
		Europi.tracks[track].channels[0].octaves = 10;			/* How many octaves are covered from scale_zero to scale_max */
		Europi.tracks[track].channels[0].vc_type = VOCT;
		/* Track 0 channel 1 = Gate Output */
		Europi.tracks[track].channels[1].enabled = TRUE;
		Europi.tracks[track].channels[1].type = CHNL_TYPE_GATE;
		Europi.tracks[track].channels[1].i2c_handle = pcf_handle;			
		Europi.tracks[track].channels[1].i2c_device = DEV_PCF8574;
		Europi.tracks[track].channels[1].i2c_channel = 0;		
		/* Track 1 channel 0 = CV */
		track++;
		Europi.tracks[track].channels[0].enabled = TRUE;
		Europi.tracks[track].channels[0].type = CHNL_TYPE_CV;
		Europi.tracks[track].channels[0].quantise = 0;		
		Europi.tracks[track].channels[0].i2c_handle = handle;			
		Europi.tracks[track].channels[0].i2c_device = DEV_DAC8574;
		Europi.tracks[track].channels[0].i2c_address = 0x08;
		Europi.tracks[track].channels[0].i2c_channel = 1;		
		Europi.tracks[track].channels[0].scale_zero = 280;		/* Value required to generate zero volt output */
		Europi.tracks[track].channels[0].scale_max = 63000;		/* Value required to generate maximum output voltage */
		Europi.tracks[track].channels[0].transpose = 0;				/* fixed (transpose) voltage offset applied to this channel */
		Europi.tracks[track].channels[0].octaves = 10;			/* How many octaves are covered from scale_zero to scale_max */
		Europi.tracks[track].channels[0].vc_type = VOCT;
		/* Track 1 channel 1 = Gate*/
		Europi.tracks[track].channels[1].enabled = TRUE;
		Europi.tracks[track].channels[1].type = CHNL_TYPE_GATE;
		Europi.tracks[track].channels[1].i2c_handle = pcf_handle;			
		Europi.tracks[track].channels[1].i2c_device = DEV_PCF8574;
		Europi.tracks[track].channels[1].i2c_channel = 1;		
		/* Channels 3 & 4 of the PCF8574 are the Clock and Step 1 out 
		 * no need to set them to anything specific, and we don't really
		 * want them appearing as additional tracks*/
		track++;	/* Minion tracks will therefore start from Track 2 */
	}
	/* 
	 * scan for Minions - these will be on addresses
	 * 0 - 7. Each Minion supports 4 Tracks
	 */
	for (address=0;address<=7;address++){
		handle = MinionFinder(address);
		if(handle >= 0){
			log_msg("Minion Found on Address %d handle = %d\n",address, handle);
			/* Get a handle to the associated MCP23008 */
			mcp_addr = MCP_BASE_ADDR | (address & 0x7);	
			gpio_handle = i2cOpen(1,mcp_addr,0);
			//log_msg("gpio_handle: %d\n",gpio_handle);
			if(gpio_handle < 0){log_msg("failed to open MCP23008 associated with DAC8574 on Addr: %0x\n",address);}
			/* Set MCP23008 IO direction to Output, and turn all Gates OFF */
			if(gpio_handle >= 0) {
				i2cWriteWordData(gpio_handle, 0x00, (unsigned)(0x0));
				i2cWriteByteData(gpio_handle, 0x09, 0x0);
				}
			int i;
			for(i=0;i<4;i++){
				//log_msg("Track: %d, channel: %d\n",track,i);
				/* Channel 0 = CV Output */
				Europi.tracks[track].channels[CV_OUT].enabled = TRUE;
				Europi.tracks[track].channels[CV_OUT].type = CHNL_TYPE_CV;
				Europi.tracks[track].channels[CV_OUT].quantise = 0;			/* Quantization Off by default */
				Europi.tracks[track].channels[CV_OUT].i2c_handle = handle;			
				Europi.tracks[track].channels[CV_OUT].i2c_device = DEV_DAC8574;
				Europi.tracks[track].channels[CV_OUT].i2c_address = address;
				Europi.tracks[track].channels[CV_OUT].i2c_channel = i;		
				Europi.tracks[track].channels[CV_OUT].scale_zero = 0X0000;	/* Value required to generate zero volt output */
				Europi.tracks[track].channels[CV_OUT].scale_max = 63000;		/* Value required to generate maximum output voltage NOTE: This needs to be overwritten during configuration*/
				Europi.tracks[track].channels[CV_OUT].transpose = 0;				/* fixed (transpose) voltage offset applied to this channel */
				Europi.tracks[track].channels[CV_OUT].octaves = 10;			/* How many octaves are covered from scale_zero to scale_max */
				Europi.tracks[track].channels[CV_OUT].vc_type = VOCT;
				/* Channel 1 = Gate Output*/
				if (gpio_handle >= 0) {
					Europi.tracks[track].channels[GATE_OUT].enabled = TRUE;
					Europi.tracks[track].channels[GATE_OUT].type = CHNL_TYPE_GATE;
					Europi.tracks[track].channels[GATE_OUT].i2c_handle = gpio_handle;			
					Europi.tracks[track].channels[GATE_OUT].i2c_device = DEV_MCP23008;
					Europi.tracks[track].channels[GATE_OUT].i2c_channel = i; 		
				}
				else {
					/* for some reason we couldn't get a handle to the MCP23008, so disable the Gate channel */
					Europi.tracks[track].channels[GATE_OUT].enabled = FALSE;
				}
				track++;
			}	
		}
	}
	/* All hardware identified - run through flashing each Gate just for fun */
	if (is_europi == TRUE){
		/* Track 0 Channel 1 will have the GPIO Handle for the PCF8574 channel 2 is Clock Out*/
		GATESingleOutput(Europi.tracks[0].channels[1].i2c_handle,CLOCK_OUT,DEV_PCF8574,HIGH);
		usleep(50000);
		GATESingleOutput(Europi.tracks[0].channels[1].i2c_handle,CLOCK_OUT,DEV_PCF8574,LOW);
		GATESingleOutput(Europi.tracks[0].channels[1].i2c_handle,STEP1_OUT,DEV_PCF8574,HIGH);
		usleep(50000);
		GATESingleOutput(Europi.tracks[0].channels[1].i2c_handle,STEP1_OUT,DEV_PCF8574,LOW);
	}

	for (track = 0;track < MAX_TRACKS; track++){
		for (chnl = 0; chnl < MAX_CHANNELS; chnl++){
			if (Europi.tracks[track].channels[chnl].enabled == TRUE){
				if (Europi.tracks[track].channels[chnl].type == CHNL_TYPE_GATE){
					GATESingleOutput(Europi.tracks[track].channels[chnl].i2c_handle, Europi.tracks[track].channels[chnl].i2c_channel,Europi.tracks[track].channels[chnl].i2c_device,1);	
					usleep(50000);
					GATESingleOutput(Europi.tracks[track].channels[chnl].i2c_handle, Europi.tracks[track].channels[chnl].i2c_channel,Europi.tracks[track].channels[chnl].i2c_device,0);	
				}
			}
		}
	}
}

/*
 * QUANTIZE
 * 
 * Takes a raw value between 0 and 60000 and returns
 * an absolute value quantized to a particular scale
 * 
 * It assumes an internal resolution of 6000 per octave
 */
int quantize(int raw, int scale){
	int octave = 0;
	int i;
	if(scale == 0) return raw;	// Scale = 0 => Quantization OFF
	if(raw > 60000) return 60000;
	while(raw > 6000){
		raw = raw - 6000;
		octave++;
	}
	for(i=0;i<=12;i++){
		if(raw >= lower_boundary[scale][i] && raw <= upper_boundary[scale][i]) return (scale_values[scale][i] + octave*6000);
	}
};

