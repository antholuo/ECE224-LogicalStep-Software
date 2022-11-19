/*
 * "Hello World" example.
 *
 * This example prints 'Hello from Nios II' to the STDOUT stream. It runs on
 * the Nios II 'standard', 'full_featured', 'fast', and 'low_cost' example
 * designs. It runs with or without the MicroC/OS-II RTOS and requires a STDOUT
 * device in your system's hardware.
 * The memory footprint of this hosted application is ~69 kbytes by default
 * using the standard reference design.
 *
 * For a reduced footprint version of this template, and an explanation of how
 * to reduce the memory footprint for a given application, see the
 * "small_hello_world" template.
 *
 */

#include <stdio.h>
#include "system.h"
#include "sys/alt_irq.h"
#include "altera_avalon_pio_regs.h"

int background() {
	int j;
	int x = 0;
	int grainsize = 4;
	int g_taskProcessed = 0;
	for (j = 0; j < grainsize; j++) {
		g_taskProcessed++;
	}
	return x;
}

// interrupts here
static void STIMULUS_IN_ISR(void* context, alt_u32 id) {
	// ISR code here
	printf("interrupt hit");

	IOWR(STIMULUS_IN_BASE, 3, 0);

	// command to clear interrupt at end of ISR
}


int main()
{
  printf("Hello from Nios II!\n");
  alt_u8 switches, buttons, leds;
  int egm_busy;

  // setup interrupts here
  alt_irq_register(STIMULUS_IN_IRQ, (void *) 0, STIMULUS_IN_ISR);

  IOWR(STIMULUS_IN_BASE, 2, 0x1);

  // EGM reg 2 is period
  IOWR(EGM_BASE, 2, 1000);

  // EGM reg 3 is pulse width
  IOWR(EGM_BASE, 3, 500);

  // enable the EGM
  IOWR(EGM_BASE, 0, 1);

  egm_busy = IORD(EGM_BASE, 1);
  while(egm_busy) {
	  egm_busy = IORD(EGM_BASE, 1);
	  printf("%d", egm_busy);
	  switches = 0x0F & (IORD(SWITCH_PIO_BASE, 0));
	  buttons = ~(IORD(BUTTON_PIO_BASE,0));
	  leds = (switches | buttons);
	  IOWR(LED_PIO_BASE, 0, leds);
	  background();
  }
  // clear egm
  IOWR(EGM_BASE, 0, 0);
  printf("\n done");
  return 0;
}
