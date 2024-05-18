#include "hardware/regs/addressmap.h"
#include "hardware/regs/io_bank0.h"
#include "hardware/regs/timer.h"
#include "hardware/regs/m0plus.h"

.syntax unified                                                 @ Specify unified assembly syntax
.cpu    cortex-m0plus                                           @ Specify CPU type is Cortex M0+
.thumb                                                          @ Specify thumb assembly for RP2040
.global main_asm                                                @ Provide program starting address to the linker
.align 4                                                        @ Specify code alignment

.equ    GPIO_BTN_F_MSK, 0x00400000   							@ Bit-22 for falling-edge event on GP21
.equ    GPIO_BTN_R_MSK, 0x00800000  							@ Bit-23 for rising-edge event on GP21

.equ    GPIO_BTN_PIN,  21              							@ Specify pin for the game button
.equ    GPIO_DIR_IN,   0              							@ Specify input direction for a GPIO pin
.equ    GPIO_DIR_OUT,  1              							@ Specify output direction for a GPIO pin

.equ    ALRM0_DFLT_TIME, 0x00180000                             @ Specify Default time for Alarm0 (space)           1.572864 seconds
.equ    ALRM1_DFLT_TIME, 0x00300000                             @ Specify Default time for Alarm1 (end sequence)    3.145728 seconds

.equ    DOT_TIME, 0x00030000                                    @ Specify Default Dot time                          0.196608 seconds

.equ    GPIO_ISR_OFFSET, 0x74         							@ GPIO is int #13
.equ    ALRM0_ISR_OFFSET, 0x40									@ ALARM0 is int #0
.equ    ALRM1_ISR_OFFSET, 0x44									@ ALARM1 is int #1




@ Entry point to the ASM portion of the program
main_asm:
    bl      init_gpio
    bl      install_isr
    bl      welcome_screen

main_loop:
	wfi															@ await incoming interrupts
	b		main_loop											@ Keep waiting in a loop




init_gpio:
	push    {lr}												@ Store return address in stack

	movs    r0, #GPIO_BTN_PIN  									@ This value is the GPIO Button Enter pin on the PI PICO board
    bl      asm_gpio_init       								@ Call the subroutine to initialise the GPIO pin specified by r0
    movs    r0, #GPIO_BTN_PIN  									@ This value is the GPIO Button Enter pin on the PI PICO board
    movs    r1, #GPIO_DIR_IN   									@ We want this GPIO pin to be setup as an output pin
    bl      asm_gpio_set_dir    								@ Call the subroutine to set the GPIO pin specified by r0 to state specified by r1
	movs    r0, #GPIO_BTN_PIN  									@ This value is the GPIO Button Enter pin on the PI PICO board
    bl      asm_gpio_set_irq       								@ Call the subroutine to initialise the GPIO pin specified by r0

    pop     {pc}                								@ Return out of the subroutine (bx lr doesn't want to work)




install_isr:
    push    {lr}                                    			@ Store return address in stack
	
	@ GPIO ISR
    ldr     r2, =(PPB_BASE + M0PLUS_VTOR_OFFSET)    			@ Load the Vector Table offset to r2
    ldr     r1, [r2]                                			@ Load the Vector Table to r1
    movs    r2, #GPIO_ISR_OFFSET                    			@ Move the GPIO ISR offset to r2
    add     r2, r1                                  			@ Set r2 to the GPIO ISR address
    ldr     r0, =gpio_isr                           			@ Load the Address of the gpio_isr subroutine to r0
    str     r0, [r2]                                			@ Set gpio_isr as the GPIO ISR subroutine

	@ ALARM0 ISR (1second, space character)
	ldr     r2, =(PPB_BASE + M0PLUS_VTOR_OFFSET)    			@ Load the Vector Table offset to r2
    ldr     r1, [r2]                                			@ Load the Vector Table to r1
    movs    r2, #ALRM0_ISR_OFFSET                    			@ Move the ALARM0 ISR offset to r2
    add     r2, r1                                  			@ Set r2 to the ALARM0 ISR address
    ldr     r0, =alarm0_isr                          			@ Load the Address of the alarm0_isr subroutine to r0
    str     r0, [r2]                                			@ Set alarm0_isr as the ALARM0 ISR subroutine

    @ ALARM1 ISR (2seconds, end sequence)
	ldr     r2, =(PPB_BASE + M0PLUS_VTOR_OFFSET)    			@ Load the Vector Table offset to r2
    ldr     r1, [r2]                                			@ Load the Vector Table to r1
    movs    r2, #ALRM1_ISR_OFFSET                    			@ Move the ALARM1 ISR offset to r2
    add     r2, r1                                  			@ Set r2 to the ALARM1 ISR address
    ldr     r0, =alarm1_isr                          			@ Load the Address of the alarm1_isr subroutine to r0
    str     r0, [r2]                                			@ Set alarm1_isr as the ALARM1 ISR subroutine

	@ Clear and Set the Interrupts
	ldr		r2, =(PPB_BASE + M0PLUS_NVIC_ICPR_OFFSET) 			@ Load NVIC Clear register
	ldr		r0, =0x2003											@ Bit 0 for TIMER_IRQ_0, Bit 1 for TIMER_IRQ_1 and Bit 13 for IO_IRQ_BANK0
	str		r0, [r2]											@ Store the Clear Mask to the NVIC Clear Register

	ldr		r2, =(PPB_BASE + M0PLUS_NVIC_ISER_OFFSET) 			@ Load NVIC Set Enable Register
	str		r0, [r2]											@ Store the Enable Mask to the Set Enable Register

    pop     {pc}                                    			@ Return out of the subroutine




.thumb_func
gpio_isr:
    push {lr}                                                   @ Store return address in stack

    ldr     r2, =(IO_BANK0_BASE + IO_BANK0_PROC0_INTS2_OFFSET)  @ Load the IO_BANK0 Interrupt State Address
    ldr     r1, [r2]                                            @ Load the actual Interrupt State Table

    ldr     r2, =GPIO_BTN_F_MSK                                 @ Load the Button Falling-Edge Mask
    ands    r2, r2, r1                                          @ Check if just pressed Button (AND result will either be the mask itself or 0)
    cmp     r2, #0x0                                            @ If AND Result is 0, then obviously button wasn't pressed
    bne     pressed                                             @ If it's not 0, then we know the button was pressed, go to subroutine

    ldr     r2, =GPIO_BTN_R_MSK                                 @ Load the Button Rising-Edge Mask
    ands    r2, r2, r1                                          @ Check if just Released Button (AND result will either be the mask itself or 0)
    cmp     r2, #0x0                                            @ If AND Result is 0, then obviously button wasn't released
    bne     released                                            @ If it's not 0, then we know the button was released, go to subroutine
    b       gpio_done                                           @ Skip to end if something weird happens and both the checks fail

released:
    @ Clear the interrupt
    ldr     r2, =(IO_BANK0_BASE + IO_BANK0_INTR2_OFFSET)        @ Load Raw Interrupts Address
    ldr     r1, =GPIO_BTN_R_MSK                                 @ Load the Button Rising-Edge Mask
    str     r1, [r2]                                            @ Clear Raw interrupts using the Button Mask

    @ Compute total hold time
    ldr		r2, =(TIMER_BASE + TIMER_TIMELR_OFFSET)				@ Load the TimeLR address
    ldr     r1, [r2]                                            @ Get the current time
    ldr     r2, =down_time                                      @ Load Press-Down time address
    ldr     r0, [r2]                                            @ Get Press-Down time

    @ Total Hold Time
    subs    r1, r1, r0                                          @ Current (Release) time - Press-Down time

    @ Check if Dot
    ldr     r2, =DOT_TIME
    cmp     r1, r2                                              @ if Hold Time < Dot Time, then we can say it's a dot
    bls     dot                                                 @ Go to Dot Subroutine

    @ Else it's a Dash
    b       dash

dot:
    @ Call C function to add a Dot to the input
    bl      add_dot

    b       released_done

dash:
    @ Call C function to add a Dot to the input
    bl      add_dash

    b       released_done

released_done:
    @ Set Alarm0 for 1s (space) & Alarm1 for 2s (end sequence)
    bl      set_next_alarm                                      @ Set the next alarm trigger

    b       gpio_done

pressed:
    @ Clear the interrupt
    ldr     r2, =(IO_BANK0_BASE + IO_BANK0_INTR2_OFFSET)        @ Load Raw Interrupts Address
    ldr     r1, =GPIO_BTN_F_MSK                                 @ Load the Button Falling-Edge Mask
    str     r1, [r2]                                            @ Clear Raw interrupts using the Button Mask

    @ Disable ALARM0 & ALARM1
    ldr		r2, =(TIMER_BASE + TIMER_INTE_OFFSET)				@ Load the Timer Set Enable register address
	movs	r1, #0x0											@ Zero to disable all
	str		r1, [r2]

    @ Store Press-Down Time
    ldr		r2, =(TIMER_BASE + TIMER_TIMELR_OFFSET)				@ Load the TimeLR address
    ldr     r1, [r2]                                            @ Get the current time
    ldr     r2, =down_time                                      @ Load Pressed-down time variable address
    str     r1, [r2]                                            @ Store current time into variable

    b       gpio_done

gpio_done:
    @ Update Watchdog
    bl      wd_enable

    pop     {pc}                                                @ Return out of the interrupt





set_next_alarm:
	push	{lr}

    @ Set the ALARM0 trigger time (in 1s from now)
	ldr		r2, =(TIMER_BASE + TIMER_TIMELR_OFFSET)				@ Load the TimeLR address
	ldr		r1, [r2]											@ load the current time from TimeLR
    ldr		r0, =ALRM0_DFLT_TIME							    @ Load Timer0 Delay address
	add		r0, r1												@ add current time plus delay

	ldr		r2, =(TIMER_BASE + TIMER_ALARM0_OFFSET)				@ Load the Alarm0 offset address
	str		r0, [r2]											@ Store the next alarm trigger to the Alarm0 register

    @ Set the ALARM1 trigger time (in 2s from now)
    ldr		r2, =(TIMER_BASE + TIMER_TIMELR_OFFSET)				@ Load the TimeLR address
	ldr		r1, [r2]											@ load the current time from TimeLR
    ldr		r0, =ALRM1_DFLT_TIME							    @ Load Timer1 Delay address
	add		r0, r1												@ add current time plus delay

	ldr		r2, =(TIMER_BASE + TIMER_ALARM1_OFFSET)				@ Load the Alarm1 offset address
	str		r0, [r2]											@ Store the next alarm trigger to the Alarm1 register

    @ Enable ALARM0 & ALARM1
    ldr		r2, =(TIMER_BASE + TIMER_INTE_OFFSET)				@ Load the Timer Set Enable register address
	movs	r1, #0x3											@ the first bit to 1 for ALARM0, the second bit to 1 for ALARM1
	str		r1, [r2]

	pop		{pc}                                    			@ Return out of the subroutine


.thumb_func
alarm0_isr:
	push	{lr}												@ Store return address in stack

    @ Clear the interrupt
	ldr		r2, =(TIMER_BASE + TIMER_INTR_OFFSET)				@ Load Raw Timer Interrupts Register Address
	ldr		r1, =0x1											@ Prepare the bit to clear in the interrupts register
	str		r1, [r2]											@ clear the bit for Alarm0 in the Raw timer interrupts register

alarm0_done:
    @ C function to finish morse sequence for a single character
    bl      end_char

    @ Update Watchdog
    bl      wd_enable

	pop		{pc}                               					@ Return out of the interrupt


.thumb_func
alarm1_isr:
	push	{lr}												@ Store return address in stack

    @ Clear the interrupt
	ldr		r2, =(TIMER_BASE + TIMER_INTR_OFFSET)				@ Load Raw Timer Interrupts Register Address
	ldr		r1, =0x2											@ Prepare the bit to clear in the interrupts register
	str		r1, [r2]											@ clear the bit for Alarm1 in the Raw timer interrupts register

alarm1_done:
    @ C function to finish Sequence
    bl      end_sequence

    @ Update Watchdog
    bl      wd_enable

	pop		{pc}                               					@ Return out of the interrupt



stop:		b	stop											@ Loop if the PC gets here


@ Set data alignment
.data
    .align 4
    down_time: .word 0x0
