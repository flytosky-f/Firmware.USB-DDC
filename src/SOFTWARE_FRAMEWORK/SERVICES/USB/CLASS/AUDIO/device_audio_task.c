/* This source file is part of the ATMEL AVR32-SoftwareFramework-AT32UC3-1.5.0 Release */

/*This file is prepared for Doxygen automatic documentation generation.*/
/*! \file ******************************************************************
 *
 * \brief Management of the USB device Audio task.
 *
 * This file manages the USB device Audio task.
 *
 * - Compiler:           IAR EWAVR32 and GNU GCC for AVR32
 * - Supported devices:  All AVR32 devices with a USB module can be used.
 * - AppNote:
 *
 * \author               Atmel Corporation: http://www.atmel.com \n
 *                       Support and FAQ: http://support.atmel.no/
 *
 ***************************************************************************/

/* Copyright (c) 2009 Atmel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. The name of Atmel may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * 4. This software may only be redistributed and used in connection with an Atmel
 * AVR product.
 *
 * THIS SOFTWARE IS PROVIDED BY ATMEL "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
 * EXPRESSLY AND SPECIFICALLY DISCLAIMED. IN NO EVENT SHALL ATMEL BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE
 *
 * Modified by Alex Lee and SDR-Widget team for the sdr-widget project.
 * See http://code.google.com/p/sdr-widget/
 * Copyright under GNU General Public License v2
 */

//_____  I N C L U D E S ___________________________________________________

#include <stdio.h>
#include "usart.h"     // Shall be included before FreeRTOS header files, since 'inline' is defined to ''; leading to
                       // link errors
#include "conf_usb.h"


#if USB_DEVICE_FEATURE == ENABLED

#include "board.h"
#ifdef FREERTOS_USED
#include "FreeRTOS.h"
#include "task.h"
#endif
#include "pdca.h"
#include "gpio.h"
#include "usb_drv.h"
#include "usb_descriptors.h"
#include "usb_standard_request.h"
#include "usb_specific_request.h"
#include "device_audio_task.h"

#if LCD_DISPLAY				// Multi-line LCD display
#include "taskLCD.h"
#endif

//#include "taskEXERCISE.h"
#include "composite_widget.h"
#include "taskAK5394A.h"

//_____ M A C R O S ________________________________________________________


//_____ D E F I N I T I O N S ______________________________________________


//_____ D E C L A R A T I O N S ____________________________________________


static volatile U16  sof_cnt;

static U32  index, spk_index;
static U16  old_gap = SPK_BUFFER_SIZE;
U8 audio_buffer_out, spk_buffer_in;	// the ID number of the buffer used for sending out
									// to the USB and reading from USB

//!
//! Public : (bit) mute
//! mute is set to TRUE when ACTIVE
//! mute is set to FALSE otherwise
//!/
volatile Bool   mute, spk_mute;
volatile U32	FB_rate;

//!
//! @brief This function initializes the hardware/software resources
//! required for device Audio task.
//!
void device_audio_task_init(void)
{
  index     =0;
  audio_buffer_out = 0;
  spk_index = 0;
  spk_buffer_in = 0;
  mute = FALSE;
  spk_mute = FALSE;

  xTaskCreate(device_audio_task,
              configTSK_USB_DAUDIO_NAME,
              configTSK_USB_DAUDIO_STACK_SIZE,
              NULL,
              configTSK_USB_DAUDIO_PRIORITY,
              NULL);


}


//!
//! @brief Entry point of the device Audio task management
//!

void device_audio_task(void *pvParameters)
{
  static U32  time=0;
  static Bool startup=TRUE;
  int i;
  U16 num_samples, num_remaining, gap;

  U8 sample_MSB;
  U8 sample_SB;
  U8 sample_LSB;
  U8 sample_HSB;
  U32 sample;


  volatile avr32_pdca_channel_t *pdca_channel = pdca_get_handler(PDCA_CHANNEL_SSC_RX);
  volatile avr32_pdca_channel_t *spk_pdca_channel = pdca_get_handler(PDCA_CHANNEL_SSC_TX);

  portTickType xLastWakeTime;
  xLastWakeTime = xTaskGetTickCount();

  while (TRUE)
  {
    vTaskDelayUntil(&xLastWakeTime, configTSK_USB_DAUDIO_PERIOD);

    // First, check the device enumeration state
    if (!Is_device_enumerated()) { time=0; startup=TRUE; continue; };

    if( startup )
     {


        time+=configTSK_USB_DAUDIO_PERIOD;
        #define STARTUP_LED_DELAY  4000
        if     ( time<= 1*STARTUP_LED_DELAY )
			{ 	LED_On( LED0 );
	        	pdca_disable_interrupt_reload_counter_zero(PDCA_CHANNEL_SSC_RX);
	            pdca_disable(PDCA_CHANNEL_SSC_RX);
			}
        else if( time== 2*STARTUP_LED_DELAY ) LED_On( LED1 );
        else if( time== 3*STARTUP_LED_DELAY ) LED_On( LED2 );
        else if( time== 4*STARTUP_LED_DELAY ) LED_On( LED3 );
        else if( time== 5*STARTUP_LED_DELAY )
		{
        	LED_Off( LED0 );
//            gpio_set_gpio_pin(AK5394_RSTN);		// start AK5394A
		}
        else if( time== 6*STARTUP_LED_DELAY ) LED_Off( LED1 );
        else if( time== 7*STARTUP_LED_DELAY ) LED_Off( LED2 );
        else if( time== 8*STARTUP_LED_DELAY ) LED_Off( LED3 );
        else if( time >= 9*STARTUP_LED_DELAY )
        	{
        	startup=FALSE;

            audio_buffer_in = 0;
            audio_buffer_out = 0;
            spk_buffer_in = 0;
            spk_buffer_out = 0;
            index = 0;

            // Wait for the next frame synchronization event
            // to avoid channel inversion.  Start with left channel - FS goes low
            while (!gpio_get_pin_value(AK5394_LRCK));
            while (gpio_get_pin_value(AK5394_LRCK));

            // Enable now the transfer.
            pdca_enable(PDCA_CHANNEL_SSC_RX);
            pdca_enable_interrupt_reload_counter_zero(PDCA_CHANNEL_SSC_RX);

            freq_changed = 1;						// force a freq change reset
        	};
     }


    if (usb_alternate_setting == 1)
    {
    	if (current_freq.frequency == 96000) num_samples = 24;
    	else if (current_freq.frequency == 48000) num_samples = 12;
    	else num_samples = 48;	// freq 192khz

		if (Is_usb_in_ready(EP_AUDIO_IN)){   // Endpoint ready for data transfer?

			Usb_ack_in_ready(EP_AUDIO_IN);	// acknowledge in ready

		        // Sync AK data stream with USB data stream
				// AK data is being filled into ~audio_buffer_in, ie if audio_buffer_in is 0
				// buffer 0 is set in the reload register of the pdca
				// So the actual loading is occuring in buffer 1
				// USB data is being taken from audio_buffer_out

				// find out the current status of PDCA transfer
				// gap is how far the audio_buffer_out is from overlapping audio_buffer_in

				num_remaining = pdca_channel->tcr;
				if (audio_buffer_in != audio_buffer_out)	// AK and USB using same buffer
				{
					if ( index < (AUDIO_BUFFER_SIZE - num_remaining)) gap = AUDIO_BUFFER_SIZE - num_remaining - index;
					else gap = AUDIO_BUFFER_SIZE - index + AUDIO_BUFFER_SIZE - num_remaining + AUDIO_BUFFER_SIZE;
				}
				else  // usb and pdca working on different buffers
				{
					gap = (AUDIO_BUFFER_SIZE - index) + (AUDIO_BUFFER_SIZE - num_remaining);
				};


				if ( gap < AUDIO_BUFFER_SIZE/2 ){				// throttle back, transfer less
					num_samples--;
				}
				else if (gap > (AUDIO_BUFFER_SIZE + AUDIO_BUFFER_SIZE/2)){	// transfer more
					num_samples++;
				};


			  Usb_reset_endpoint_fifo_access(EP_AUDIO_IN);
			  for( i=0 ; i < num_samples ; i++ )   // Fill endpoint with samples
			  {
				 if(!mute)
				 {

					if (audio_buffer_out == 0)
					{
						sample_LSB = audio_buffer_0[index];
						sample_SB = audio_buffer_0[index] >> 8;
						sample_MSB = audio_buffer_0[index] >> 16;
					}
					else
					{
						sample_LSB = audio_buffer_1[index];
						sample_SB = audio_buffer_1[index] >> 8;
						sample_MSB = audio_buffer_1[index] >> 16;
					};

					Usb_write_endpoint_data(EP_AUDIO_IN, 8, sample_LSB);
					Usb_write_endpoint_data(EP_AUDIO_IN, 8, sample_SB);
					Usb_write_endpoint_data(EP_AUDIO_IN, 8, sample_MSB);


					if (audio_buffer_out == 0)
					{
						sample_LSB = audio_buffer_0[index+1];
						sample_SB = audio_buffer_0[index+1] >> 8;
						sample_MSB = audio_buffer_0[index+1] >> 16;
					}
					else
					{
						sample_LSB = audio_buffer_1[index+1];
						sample_SB = audio_buffer_1[index+1] >> 8;
						sample_MSB = audio_buffer_1[index+1] >> 16;
					};

					Usb_write_endpoint_data(EP_AUDIO_IN, 8, sample_LSB);
					Usb_write_endpoint_data(EP_AUDIO_IN, 8, sample_SB);
					Usb_write_endpoint_data(EP_AUDIO_IN, 8, sample_MSB);

					index += 2;
					if (index >= AUDIO_BUFFER_SIZE)
					{
						index=0;
						audio_buffer_out = 1 - audio_buffer_out;
					};
				 }
				 else
				 {
						Usb_write_endpoint_data(EP_AUDIO_IN, 8, 0x00);
						Usb_write_endpoint_data(EP_AUDIO_IN, 8, 0x00);
						Usb_write_endpoint_data(EP_AUDIO_IN, 8, 0x00);
						Usb_write_endpoint_data(EP_AUDIO_IN, 8, 0x00);
						Usb_write_endpoint_data(EP_AUDIO_IN, 8, 0x00);
						Usb_write_endpoint_data(EP_AUDIO_IN, 8, 0x00);

				 };
			  };
			  Usb_send_in(EP_AUDIO_IN);		// send the current bank
		   };
    } // end alt setting 1

	if (usb_alternate_setting_out == 1){


			if (Is_usb_in_ready(EP_AUDIO_OUT_FB)){   // Endpoint buffer free ?
				Usb_ack_in_ready(EP_AUDIO_OUT_FB);	// acknowledge in ready


				Usb_reset_endpoint_fifo_access(EP_AUDIO_OUT_FB);
				// Sync CS4344 spk data stream by calculating gap and provide feedback
				num_remaining = spk_pdca_channel->tcr;
				if (spk_buffer_in != spk_buffer_out)	// CS4344 and USB using same buffer
				{
					if ( spk_index < (SPK_BUFFER_SIZE - num_remaining)) gap = SPK_BUFFER_SIZE - num_remaining - spk_index;
					else gap = SPK_BUFFER_SIZE - spk_index + SPK_BUFFER_SIZE - num_remaining + SPK_BUFFER_SIZE;
				}
				else  // usb and pdca working on different buffers
				{
					gap = (SPK_BUFFER_SIZE - spk_index) + (SPK_BUFFER_SIZE - num_remaining);
				}


				if (Is_usb_full_speed_mode()){			// FB rate is 3 bytes in 10.14 format

					if ((gap < (SPK_BUFFER_SIZE/2)) && (gap < old_gap)){
						LED_Toggle(LED0);
						FB_rate -= 2;
						old_gap = gap;
					}
					else if ( (gap > (SPK_BUFFER_SIZE + (SPK_BUFFER_SIZE/2))) && (gap > old_gap)){
						LED_Toggle(LED1);
						FB_rate += 2;
						old_gap = gap;
					}

					sample_LSB = FB_rate;
					sample_SB = FB_rate >> 8;
					sample_MSB = FB_rate >> 16;
					Usb_write_endpoint_data(EP_AUDIO_OUT_FB, 8, sample_LSB);
					Usb_write_endpoint_data(EP_AUDIO_OUT_FB, 8, sample_SB);
					Usb_write_endpoint_data(EP_AUDIO_OUT_FB, 8, sample_MSB);
				}
				 else	// HS mode
				{									// FB rate is 4 bytes in 12.13 format

						if ((gap < (SPK_BUFFER_SIZE/2)) && (gap < old_gap)){
							LED_Toggle(LED0);
							FB_rate -= 5;
							old_gap = gap;
						}
						else if ( (gap > (SPK_BUFFER_SIZE + (SPK_BUFFER_SIZE/2))) && (gap > old_gap)){
							LED_Toggle(LED1);
							FB_rate += 5;
							old_gap = gap;
						}

						sample_LSB = FB_rate;
						sample_SB = FB_rate >> 8;
						sample_MSB = FB_rate >> 16;
						sample_HSB = FB_rate >> 24;
						Usb_write_endpoint_data(EP_AUDIO_OUT_FB, 8, sample_LSB);
						Usb_write_endpoint_data(EP_AUDIO_OUT_FB, 8, sample_SB);
						Usb_write_endpoint_data(EP_AUDIO_OUT_FB, 8, sample_MSB);
						Usb_write_endpoint_data(EP_AUDIO_OUT_FB, 8, sample_HSB);
				}

				Usb_send_in(EP_AUDIO_OUT_FB);
			} // end sub_in_ready


		if (Is_usb_out_received(EP_AUDIO_OUT)){


			Usb_reset_endpoint_fifo_access(EP_AUDIO_OUT);
			num_samples = Usb_byte_count(EP_AUDIO_OUT) / 6;

			for (i = 0; i < num_samples; i++){
				if (spk_mute) {
					sample_LSB = 0;
					sample_SB = 0;
					sample_MSB = 0;
				} else {
					sample_LSB = Usb_read_endpoint_data(EP_AUDIO_OUT, 8);
					sample_SB = Usb_read_endpoint_data(EP_AUDIO_OUT, 8);
					sample_MSB = Usb_read_endpoint_data(EP_AUDIO_OUT, 8);
				};

				sample = (((U32) sample_MSB) << 16) + (((U32)sample_SB) << 8) + sample_LSB;
				if (spk_buffer_in == 0) spk_buffer_0[spk_index] = sample;
				else spk_buffer_1[spk_index] = sample;

				if (spk_mute) {
					sample_LSB = 0;
					sample_SB = 0;
					sample_MSB = 0;
				} else {
					sample_LSB = Usb_read_endpoint_data(EP_AUDIO_OUT, 8);
					sample_SB = Usb_read_endpoint_data(EP_AUDIO_OUT, 8);
					sample_MSB = Usb_read_endpoint_data(EP_AUDIO_OUT, 8);
				};

				sample = (((U32) sample_MSB) << 16) + (((U32)sample_SB) << 8) + sample_LSB;
				if (spk_buffer_in == 0) spk_buffer_0[spk_index+1] = sample;
				else spk_buffer_1[spk_index+1] = sample;

				spk_index += 2;
				if (spk_index >= SPK_BUFFER_SIZE){
					spk_index = 0;
					spk_buffer_in = 1 - spk_buffer_in;
				}
			}
			Usb_ack_out_received_free(EP_AUDIO_OUT);
	   }	// end usb_out_received
	} // end usb_alternate_setting_out == 1

  } // end while vTask

}


//!
//! @brief usb_sof_action
//!
//! This function increments the sof_cnt counter each time
//! the USB Start-of-Frame interrupt subroutine is executed (1 ms).
//! Useful to manage time delays
//!
void usb_sof_action(void)
{
  sof_cnt++;
}


#endif  // USB_DEVICE_FEATURE == ENABLED