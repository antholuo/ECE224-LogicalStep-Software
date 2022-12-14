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
#include "sys/alt_irq.h"
#include "system.h"
#include "altera_avalon_pio_regs.h"

static int button_click = 0;

static void button_ISR(void *context, alt_u32 id)
{
  // isr code here
  // disable button interrupts
  static int counter = 0;
  IOWR(BUTTON_PIO_BASE, 2, 0x0);
  int buttons = IORD(BUTTON_PIO_BASE, 0);
  IOWR(TIMER_0_BASE, 1, 0b0111);
  if (buttons == 0b1110) {
	  printf("1\n");
  }
  if (buttons == 0b1101) {
	  printf("2\n");
  }
  if (buttons == 0b1011) {
	  printf("3\n");
  }
  if (buttons == 0b0111) {
	  printf("4\n");
  }
  printf("button hit %d %d\n", counter, buttons);
  counter += 1;
  IOWR(BUTTON_PIO_BASE, 3, 0x0);
  //   clear interrupt
}

static void timer_ISR(void *context, alt_u32 id)
{
  // timer isr code here
  IOWR(BUTTON_PIO_BASE, 3, 0x0);
  IOWR(BUTTON_PIO_BASE, 2, 0xf);
  printf("timer isr hit \n");
  // clear timer TO
  IOWR(TIMER_0_BASE, 0, 0x0);
  IOWR(TIMER_0_BASE, 1, 0b1011);
}

int main()
{
  printf("Hello from Nios II!\n");

  alt_irq_register(BUTTON_PIO_IRQ, (void *)0, button_ISR);

  IOWR(BUTTON_PIO_BASE, 3, 0x0);

  IOWR(BUTTON_PIO_BASE, 2, 0xf);

  // write timer periods
  IOWR(TIMER_0_BASE, 2, 0xffff);
  IOWR(TIMER_0_BASE, 3, 0x00ff);
  IOWR(TIMER_0_BASE, 4, 0x0000);
  IOWR(TIMER_0_BASE, 5, 0x0000);

  // set timer cont 0, IRQ en
  IOWR(TIMER_0_BASE, 1, 0x3);

  // register timer IRQ
  alt_irq_register(TIMER_0_IRQ, (void *)0, timer_ISR);

  while (1)
  {
  }

  return 0;
}
