/* tuxctl-ioctl.c
 *
 * Driver (skeleton) for the mp2 tuxcontrollers for ECE391 at UIUC.
 *
 * Mark Murphy 2006
 * Andrew Ofisher 2007
 * Steve Lumetta 12-13 Sep 2009
 * Puskar Naha 2013
 */

#include <asm/current.h>
#include <asm/uaccess.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/miscdevice.h>
#include <linux/kdev_t.h>
#include <linux/tty.h>
#include <linux/spinlock.h>
//#include <stdint.h>

#include "tuxctl-ld.h"
#include "tuxctl-ioctl.h"
#include "mtcp.h"

#define debug(str, ...) \
	printk(KERN_DEBUG "%s: " str, __FUNCTION__, ## __VA_ARGS__)

// Globals. All of these are proteted by biglock
static unsigned char buttons;
static unsigned char sent_sig;
static unsigned long led_state;
//static spinlock_t biglock;
// See function bodies for details
void handle_buttons(unsigned char first, unsigned char second);
int handle_reset(struct tty_struct* tty);
int tuxctl_ioctl_init(struct tty_struct* tty);
int tuxctl_ioctl_buttons(struct tty_struct *tty, unsigned long arg);
int tuxctl_ioctl_set_led(struct tty_struct *tty, unsigned long arg);
char led_convert(unsigned long convertee);
/************************ Protocol Implementation *************************/

/* tuxctl_handle_packet()
 * IMPORTANT : Read the header for tuxctl_ldisc_data_callback() in 
 * tuxctl-ld.c. It calls this function, so all warnings there apply 
 * here as well.
 */
void tuxctl_handle_packet (struct tty_struct* tty, unsigned char* packet)
{
    unsigned a, b, c;
    
    //packet[0] is opcode
    a = packet[0]; /* Avoid printk() sign extending the 8-bit */
    b = packet[1]; /* values when printing them. */
    c = packet[2];
    //printk("handler called: %x %x %x \n", a, b, c);
    switch(a){
    	case MTCP_BIOC_EVENT:
    		handle_buttons(b, c); //call bioc event handler
    		//printk("MTCP_BIOC_EVENT \n");
    		break;
    	case MTCP_ACK:
    		sent_sig = 0; // Mark this flag as 0
    		//printk("MTCP_ACK \n");
    		break;
    	case MTCP_RESET:
    		handle_reset(tty); //call reset handler
    		//printk("MTCP_RESET \n");
    		break;
    		//tuxctl_ioctl_set_led(tty, led_state);
    	default:
    		return;
    }

    /*printk("packet : %x %x %x\n", a, b, c); */
}

/* 
 * handle_reset
 *   DESCRIPTION: Handles when MTCP_RESET is sent from the TUX
 *   INPUTS: tty - this is a variable that is passed in struct useless
 *   OUTPUTS: none
 *   RETURN VALUE: None
 *   SIDE EFFECTS: Writes the former led_state back to the display
 */
int handle_reset(struct tty_struct* tty)
{
	//tuxctl_ioctl_init(tty);
	unsigned char b[2] = {MTCP_LED_USR, MTCP_BIOC_ON}; //values to tux
	tuxctl_ldisc_put(tty, b, 2); //write to tux
	if(sent_sig == 1)
		return -EINVAL;
	sent_sig = 1; //Set flag indicating that a signal has been sent
	tuxctl_ioctl_set_led(tty, led_state);
	return 0;
	//printk("handle_reset called. \n");
}

/* 
 * handle_buttons
 *   DESCRIPTION: Handles when MTCP_BIOC_EVENT is sent from the TUX
 *   INPUTS: first - the first byte that is sent (contains A, B, C, Start)
 *			 second - the second byte that is sent (contains R, L D, U)
 *   OUTPUTS: none
 *   RETURN VALUE: None
 *   SIDE EFFECTS: Writes the combined values of inputs to buttons
 */
void handle_buttons(unsigned char first, unsigned char second)
{
	unsigned char temp1 = first & 0x0F;//right, left, down, up, c, b, a, start
	unsigned char temp2 = ((second & 0x0F) << 4); //right and up in correct place
	buttons  = temp1 | temp2; // Single byte value to represent all buttons
	//printk("handle_buttons called \n");

}

/******** IMPORTANT NOTE: READ THIS BEFORE IMPLEMENTING THE IOCTLS ************
 *                                                                            *
 * The ioctls should not spend any time waiting for responses to the commands *
 * they send to the controller. The data is sent over the serial line at      *
 * 9600 BAUD. At this rate, a byte takes approximately 1 millisecond to       *
 * transmit; this means that there will be about 9 milliseconds between       *
 * the time you request that the low-level serial driver send the             *
 * 6-byte SET_LEDS packet and the time the 3-byte ACK packet finishes         *
 * arriving. This is far too long a time for a system call to take. The       *
 * ioctls should return immediately with success if their parameters are      *
 * valid.                                                                     *
 *                                                                            *
 ******************************************************************************/

int 
tuxctl_ioctl (struct tty_struct* tty, struct file* file, 
	      unsigned cmd, unsigned long arg)
{
	// Go to the proper handler based on the command
    switch (cmd) {
	case TUX_INIT:
		return tuxctl_ioctl_init(tty); // Initializer
		break;
	case TUX_BUTTONS:
		return tuxctl_ioctl_buttons(tty, arg); // Returns buttons
		break;
	case TUX_SET_LED:
		return tuxctl_ioctl_set_led(tty, arg); // Set LEDs
		break;
	default:
	    return -EINVAL; // Invalid command
    }
    return 0;
}

/* 
 * tuxctl_ioctl_init
 *   DESCRIPTION: Initializes the TUX for usage
 *   INPUTS: tty - this is a variable that is passed in struct useless
 *   OUTPUTS: none
 *   RETURN VALUE: Returns 0 on success
 *   SIDE EFFECTS: Writes codes to the TUX for initializing display and interrupts
 */
int
tuxctl_ioctl_init(struct tty_struct* tty)
{
	unsigned char a = MTCP_BIOC_ON; // values to write
	unsigned char b = MTCP_LED_USR;
	sent_sig = 1;
	tuxctl_ldisc_put(tty, &b, 1); //write the values
	tuxctl_ldisc_put(tty, &a, 1);
	buttons = 0xFF;
	led_state = 0;
	//spin_unlock(&biglock);
	//printk("ioctl init called \n");
	return 0;
}

/* 
 * tuxctl_ioctl_buttons
 *   DESCRIPTION: Gives the user the pressed buttons
 *   INPUTS: tty - this is a variable that is passed in struct useless
 *			 arg - this is a pointer to a user space location to write the 
 *				   button presses to.
 *   OUTPUTS: none
 *   RETURN VALUE: Returns 0 on success or -EINVAL is pointer invalid
 *   SIDE EFFECTS: a byte of data to the passed pointer
 */
int tuxctl_ioctl_buttons(struct tty_struct *tty, unsigned long arg)
{
	unsigned long* ptr = (unsigned long*)arg; // Case a pointer
	if(ptr == NULL)
		return -EINVAL; // Invalid pointer = you suck
	//spin_lock(&biglock);
	copy_to_user(ptr, &buttons, 1); // give user the buttons
	//spin_unlock(&biglock);
	//printk("ioctl buttons called : %hu \n", buttons);
	return 0;
	
}

/* 
 * tuxctl_ioctl_set_led
 *   DESCRIPTION: Sets the leds
 *   INPUTS: tty - this is a variable that is passed in struct useless
 *			 arg - The argument is a 32-bit integer of the following form: 
 *				   The low 16-bits specify a number whose hexadecimal value is
 *				   to be displayed on the 7-segment displays. The low 4 bits of 
 *				   the third byte specifies which LED’s should be turned on. 
 *				   The low 4 bits of the highest byte (bits 27:24) specify
 *				   whether the corresponding decimal points should be turned on.
 *   OUTPUTS: none
 *   RETURN VALUE: Returns 0 on success
 *   SIDE EFFECTS: Writes to the LED 7 segment displays based on arg
 */
int
tuxctl_ioctl_set_led(struct tty_struct *tty, unsigned long arg)
{
	unsigned int num = arg & 0x0000FFFF; // Masks the digits
	unsigned long a, b; // Counters
	// These are the decimal digits to be represented
	unsigned long digit[4]; // Represents the four digits to be displayed
	unsigned char dec[4];
	unsigned char buf[6]; // The stuff to write
	//unsigned char clear[6];
	//Figure out the decimal stuff
	for(a = 0; a < 4; a++)
	{
		if(arg & (0x01000000 << a))
			dec[a] = 1;
		else
			dec[a] = 0;
	}
	//Get each digit
	digit[0] = num & 0x000F;
	digit[1] = (num & 0x00F0) >> 4;
	digit[2] = (num & 0x0F00) >> 8;
	digit[3] = (num & 0xF000) >> 12;
	buf[0] = MTCP_LED_SET; // Opcode to send
	buf[1] = 0x0F; // Write to all spots
	for(a = 0; a < 4; a++)
	{
		b = a + 2;
		if(arg & (0x00010000 << a))
		{ //if LED is to be turned on
			buf[b] = led_convert(digit[a]); // Get the 7-segment values
			if(dec[a] == 1)
			{
				buf[b] = buf[b] | 0x10; // Add decimals where necessary
			}//b++;
		}
		else
		{
			buf[b] = 0; // Write a space
		}
	}
	//check flag and write
	if(sent_sig == 1)
		return -EINVAL;
	sent_sig = 1;
	tuxctl_ldisc_put(tty, buf, 6); // Write!
	led_state = arg; // Set state of LEDs
	//sent_led_flag = 1;
	//printk("set_led called: %lx \n", arg);
	return 0;
}

/* 
 * led_convert
 *   DESCRIPTION: Converts a hex value to a 7-segment value with no decimal
 *   INPUTS: convertee - this a hex value that we want to display
 *   OUTPUTS: A char for writing to the 7 segment displays
 *   RETURN VALUE: Returns 0 if the value is not between 0-F
 *   SIDE EFFECTS: None
 */
char led_convert(unsigned long convertee)
{
	// This switch statement covers all the display values for hex
	switch(convertee){
		case 0:
			return 0xE7;
		case 1:
			return 0x06;
		case 2:
			return 0xCB;
		case 3:
			return 0x8F;
		case 4:
			return 0x2E;
		case 5:
			return 0xAD;
		case 6:
			return 0xED;
		case 7:
			return 0x86;
		case 8:
			return 0xEF;
		case 9:
			return 0xAF;
		case 10:
			return 0xEE;
		case 11:
			return 0x6D;
		case 12:
			return 0xE1;
		case 13:
			return 0x4F;
		case 14:
			return 0xE9;
		case 15:
			return 0xE8;
		default:
			return 0;
	}
}
