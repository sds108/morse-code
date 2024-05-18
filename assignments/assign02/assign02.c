#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "pico/float.h"                 // Required for using single-precision variables.
#include "pico/double.h"                // Required for using double-precision variables.
#include "hardware/pio.h"
#include "ws2812.pio.h"
#include "hardware/watchdog.h"

#define IS_RGBW true        // Will use RGBW format
#define NUM_PIXELS 1        // There is 1 WS2812 device in the chain
#define WS2812_PIN 28       // The GPIO pin that the WS2812 connected to

/**
 * @brief Wrapper function used to call the underlying PIO
 *        function that pushes the 32-bit RGB colour value
 *        out to the LED serially using the PIO0 block. The
 *        function does not return until all of the data has
 *        been written out.
 * 
 * @param pixel_grb The 32-bit colour value generated by urgb_u32()
 */
static inline void put_pixel(uint32_t pixel_grb) {
    pio_sm_put_blocking(pio0, 0, pixel_grb << 8u);
}


/**
 * @brief Function to generate an unsigned 32-bit composit GRB
 *        value by combining the individual 8-bit paramaters for
 *        red, green and blue together in the right order.
 * 
 * @param r     The 8-bit intensity value for the red component
 * @param g     The 8-bit intensity value for the green component
 * @param b     The 8-bit intensity value for the blue component
 * @return uint32_t Returns the resulting composit 32-bit RGB value
 */
static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
    return  ((uint32_t) (r) << 8)  |
            ((uint32_t) (g) << 16) |
            (uint32_t) (b);
}




char char_array[] = {
    // Digits 0 - 9
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 
    // Letters A - Z
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z'};
char morse_table[36][6] = { // Must declare as a char pointer array to get an array of strings since this is in C not C++
    // Digits 0 - 9
    "-----\0", ".----\0", "..---\0", "...--\0", "....-\0", ".....\0",
    "-....\0", "--...\0", "---..\0", "----.\0", 
    // Letters A - Z
    ".-\0", "-...\0", "-.-.\0", "-..\0", ".\0", "..-.\0", "--.\0", "....\0",
    "..\0", ".---\0", "-.-\0", ".-..\0", "--\0", "-.\0", "---\0", ".--.\0",
    "--.-\0", ".-.\0", "...\0", "-\0", "..-\0", "...-\0", ".--\0", "-..-\0",
    "-.--\0", "--..\0",
}; 


// Game Variables
#define MAX_LIVES 3
#define CONSECUTIVE_TO_WIN 5
#define MAX_SIZE 5
int right_input = 0;
int lives = MAX_LIVES;
int incorrect = 0;
int correct = 0;
int attempts = 0;

int levelsCompleted[4]= {0,0,0,0};

/*
    State Variables

    Modes:
        0:  Home Screen (Level Select)
        1:  Game

    Levels:
        1: Print Morse Equivalent
        2: Don't Print Morse Equivalent
        3: Print Morse Equivalent
        4: Don't Print Morse Equivalent
*/

int mode = 0;
int level = 0;

// Level 1 & 2 Variables
int rand_num;

// Function declarations
void add_dot();
void add_dash();
void end_char();
void end_sequence();
void add_char();
void state_processor(int size);
void choose_expected();
void print_expected();
void clear_screen();
void update_LED();
void stats();

// Volatile Global Variables for Input Handling
#define MAX_MORSE_INPUT 20
#define MAX_INPUT 200
char morse_input[MAX_MORSE_INPUT];
int morse_index = 0;
char input[MAX_INPUT];
int input_index = 0;

/* ---FUNCTIONS--- */


// Must declare the main assembly entry point before use.
void main_asm();

void watchdog_update();
void watchdog_enable(uint32_t delay_ms, bool pause_on_debug);

void wd_enable() {
    watchdog_enable(9000, true);
}

void reset_game_params() {
    lives = MAX_LIVES;
    right_input = 0;
    incorrect = 0;
    correct = 0;
    attempts = 0;
}

void upper_edge() {
    printf("\n░\n");
    printf("▒░\n");
    printf("▓▒░\n");
}

void lower_edge() {
    printf("▓▒░\n");
    printf("▒░\n");
    printf("░\n\n");
}

void rules() {
    printf("█▓▒░\n");
    printf("█▓▒░ The rules are as follows:\n");
    printf("█▓▒░ 1. Enter the character displayed in morse\n");
    printf("█▓▒░ 2. If you get it correct you gain a life\n");
    printf("█▓▒░ 3. Otherwise you lose a life. The LED will indicate how many lives you have\n");
    printf("█▓▒░ 4. If you take longer than 9 seconds to input a character the game will reset\n");
    printf("█▓▒░ 5. If you lose all 3 lives the game will end\n");
}

void menu_screen() {
    printf("█▓▒░ USE GP21 TO ENTER A SEQUENCE TO BEGIN\n");
    printf("█▓▒░ \".----\" - LEVEL 01 - CHARS (EASY) %s\n", levelsCompleted[0] ? "(Completed)" : "           ");
    printf("█▓▒░ \"..---\" - LEVEL 02 - CHARS (HARD) %s\n", levelsCompleted[1] ? "(Completed)" : "           ");
    printf("█▓▒░ \"...--\" - LEVEL 03 - WORDS (EASY) %s\n", levelsCompleted[2] ? "(Completed)" : "           ");
    printf("█▓▒░ \"....-\" - LEVEL 04 - WORDS (HARD) %s\n", levelsCompleted[3] ? "(Completed)" : "           ");
}

// Print the opening screen with rules explaining the game
void welcome_screen() {
    // Update LED Colour
    clear_screen();
    mode = 0;
    update_LED();

    printf("\033[1;32m");
    printf("░▒▓██████████████▓▒░   ░▒▓██████▓▒░  ░▒▓███████▓▒░   ░▒▓███████▓▒░ ░▒▓████████▓▒░\n");
    printf("░▒▓█▓▒░░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░        ░▒▓█▓▒░\n");
    printf("░▒▓█▓▒░░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░        ░▒▓█▓▒░\n");
    printf("░▒▓█▓▒░░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓███████▓▒░   ░▒▓██████▓▒░  ░▒▓██████▓▒░\n");
    printf("░▒▓█▓▒░░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░        ░▒▓█▓▒░ ░▒▓█▓▒░\n");
    printf("░▒▓█▓▒░░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░        ░▒▓█▓▒░ ░▒▓█▓▒░\n");
    printf("░▒▓█▓▒░░▒▓█▓▒░░▒▓█▓▒░  ░▒▓██████▓▒░░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓███████▓▒░  ░▒▓████████▓▒░\n");
    printf("\n");
    printf("\n");
    printf("           ░▒▓██████▓▒░   ░▒▓██████▓▒░  ░▒▓███████▓▒░  ░▒▓████████▓▒░\n");
    printf("          ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░\n");
    printf("          ░▒▓█▓▒░        ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░\n");
    printf("          ░▒▓█▓▒░        ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓██████▓▒░\n");
    printf("          ░▒▓█▓▒░        ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░\n");
    printf("          ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░\n");
    printf("           ░▒▓██████▓▒░   ░▒▓██████▓▒░  ░▒▓███████▓▒░  ░▒▓████████▓▒░\n");

    reset_game_params();

    upper_edge();
    menu_screen();
    rules();
    lower_edge();
}

void end_screen() {
    // Update LED Colour
    clear_screen();
    mode = 0;
    update_LED();

    printf("   ░▒▓█▓▒░░▒▓█▓▒░  ░▒▓██████▓▒░  ░▒▓█▓▒░░▒▓█▓▒░\n");
    printf("   ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░\n");
    printf("   ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░\n");
    printf("    ░▒▓██████▓▒░  ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░\n");
    printf("      ░▒▓█▓▒░     ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░\n");
    printf("      ░▒▓█▓▒░     ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░\n");
    printf("      ░▒▓█▓▒░      ░▒▓██████▓▒░   ░▒▓██████▓▒░\n");
    printf("\n");
    printf("\n");
    printf("░▒▓█▓▒░░▒▓█▓▒░░▒▓█▓▒░  ░▒▓██████▓▒░  ░▒▓███████▓▒░\n");
    printf("░▒▓█▓▒░░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░\n");
    printf("░▒▓█▓▒░░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░\n");
    printf("░▒▓█▓▒░░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░\n");
    printf("░▒▓█▓▒░░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░\n");
    printf("░▒▓█▓▒░░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░\n");
    printf(" ░▒▓█████████████▓▒░   ░▒▓██████▓▒░  ░▒▓█▓▒░░▒▓█▓▒░\n");

    upper_edge();
    stats();
    reset_game_params();
    menu_screen();
    lower_edge();
}

void losing_screen() {
    // Update LED Colour
    clear_screen();
    mode = 0;
    update_LED();
                                      
    printf("  ▄████  ▄▄▄      ███▄ ▄███▓▓█████\n");  
    printf(" ██▒ ▀█▒▒████▄   ▓██▒▀█▀ ██▒▓█   ▀\n");  
    printf("▒██░▄▄▄░▒██  ▀█▄ ▓██    ▓██░▒███\n");  
    printf("░▓█  ██▓░██▄▄▄▄██▒██    ▒██ ▒▓█  ▄\n");  
    printf("░▒▓███▀▒ ▓█   ▓██▒██▒   ░██▒░▒████▒\n");  
    printf(" ░▒   ▒  ▒▒   ▓▒█░ ▒░   ░  ░░░ ▒░ ░\n");  
    printf("  ░   ░   ▒   ▒▒ ░  ░      ░ ░ ░  ░\n");  
    printf("░ ░   ░   ░   ▒  ░      ░      ░\n");  
    printf("      ░       ░  ░      ░      ░  ░\n");  
    printf("\n");  
    printf(" ▒█████   ██▒   █▓▓█████  ██▀███\n");  
    printf("▒██▒  ██▒▓██░   █▒▓█   ▀ ▓██ ▒ ██▒\n");  
    printf("▒██░  ██▒ ▓██  █▒░▒███   ▓██ ░▄█ ▒\n");  
    printf("▒██   ██░  ▒██ █░░▒▓█  ▄ ▒██▀▀█▄\n");  
    printf("░ ████▓▒░   ▒▀█░  ░▒████▒░██▓ ▒██▒\n");  
    printf("░ ▒░▒░▒░    ░ ▐░  ░░ ▒░ ░░ ▒▓ ░▒▓░\n");  
    printf("  ░ ▒ ▒░    ░ ░░   ░ ░  ░  ░▒ ░ ▒░\n");  
    printf("░ ░ ░ ▒       ░░     ░     ░░   ░\n");  
    printf("    ░ ░        ░     ░  ░   ░\n");  
    printf("              ░\n");                         

    upper_edge();
    stats();
    reset_game_params();
    menu_screen();
    lower_edge();
}

void level_complete_screen() {
    // Update LED Colour
    clear_screen();
    mode = 0;
    update_LED();

    printf("                        ░▒▓█▓▒░        ░▒▓████████▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓████████▓▒░ ░▒▓█▓▒░\n");
    printf("                        ░▒▓█▓▒░        ░▒▓█▓▒░        ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░        ░▒▓█▓▒░\n");
    printf("                        ░▒▓█▓▒░        ░▒▓█▓▒░         ░▒▓█▓▒▒▓█▓▒░  ░▒▓█▓▒░        ░▒▓█▓▒░\n");
    printf("                        ░▒▓█▓▒░        ░▒▓██████▓▒░    ░▒▓█▓▒▒▓█▓▒░  ░▒▓██████▓▒░   ░▒▓█▓▒░\n");
    printf("                        ░▒▓█▓▒░        ░▒▓█▓▒░          ░▒▓█▓▓█▓▒░   ░▒▓█▓▒░        ░▒▓█▓▒░\n");
    printf("                        ░▒▓█▓▒░        ░▒▓█▓▒░          ░▒▓█▓▓█▓▒░   ░▒▓█▓▒░        ░▒▓█▓▒░\n");
    printf("                        ░▒▓████████▓▒░ ░▒▓████████▓▒░    ░▒▓██▓▒░    ░▒▓████████▓▒░ ░▒▓████████▓▒░\n");
    printf("\n");
    printf("\n");
    printf(" ░▒▓██████▓▒░   ░▒▓██████▓▒░  ░▒▓██████████████▓▒░  ░▒▓███████▓▒░  ░▒▓█▓▒░        ░▒▓████████▓▒░ ░▒▓████████▓▒░ ░▒▓████████▓▒░\n");
    printf("░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░        ░▒▓█▓▒░           ░▒▓█▓▒░     ░▒▓█▓▒░\n");
    printf("░▒▓█▓▒░        ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░        ░▒▓█▓▒░           ░▒▓█▓▒░     ░▒▓█▓▒░\n");
    printf("░▒▓█▓▒░        ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░░▒▓█▓▒░ ░▒▓███████▓▒░  ░▒▓█▓▒░        ░▒▓██████▓▒░      ░▒▓█▓▒░     ░▒▓██████▓▒░\n");
    printf("░▒▓█▓▒░        ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░        ░▒▓█▓▒░        ░▒▓█▓▒░           ░▒▓█▓▒░     ░▒▓█▓▒░\n");
    printf("░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░        ░▒▓█▓▒░        ░▒▓█▓▒░           ░▒▓█▓▒░     ░▒▓█▓▒░\n");
    printf(" ░▒▓██████▓▒░   ░▒▓██████▓▒░  ░▒▓█▓▒░░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░        ░▒▓████████▓▒░ ░▒▓████████▓▒░    ░▒▓█▓▒░     ░▒▓████████▓▒░\n");

    upper_edge();
    stats();
    reset_game_params();
    menu_screen();
    lower_edge();
}

void correct_screen() {
    clear_screen();

    printf(" ░▒▓██████▓▒░   ░▒▓██████▓▒░  ░▒▓███████▓▒░  ░▒▓███████▓▒░  ░▒▓████████▓▒░  ░▒▓██████▓▒░  ░▒▓████████▓▒░\n");
    printf("░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░        ░▒▓█▓▒░░▒▓█▓▒░    ░▒▓█▓▒░\n");
    printf("░▒▓█▓▒░        ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░        ░▒▓█▓▒░           ░▒▓█▓▒░\n");
    printf("░▒▓█▓▒░        ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓███████▓▒░  ░▒▓███████▓▒░  ░▒▓██████▓▒░   ░▒▓█▓▒░           ░▒▓█▓▒░\n");
    printf("░▒▓█▓▒░        ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░        ░▒▓█▓▒░           ░▒▓█▓▒░\n");
    printf("░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░        ░▒▓█▓▒░░▒▓█▓▒░    ░▒▓█▓▒░\n");
    printf(" ░▒▓██████▓▒░   ░▒▓██████▓▒░  ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓████████▓▒░  ░▒▓██████▓▒░     ░▒▓█▓▒░\n");
}

void incorrect_screen() {
    clear_screen();

    printf("░▒▓█▓▒░ ░▒▓███████▓▒░   ░▒▓██████▓▒░   ░▒▓██████▓▒░  ░▒▓███████▓▒░  ░▒▓███████▓▒░  ░▒▓████████▓▒░  ░▒▓██████▓▒░  ░▒▓████████▓▒░\n");
    printf("░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░        ░▒▓█▓▒░░▒▓█▓▒░    ░▒▓█▓▒░\n");
    printf("░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░        ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░        ░▒▓█▓▒░           ░▒▓█▓▒░\n");
    printf("░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░        ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓███████▓▒░  ░▒▓███████▓▒░  ░▒▓██████▓▒░   ░▒▓█▓▒░           ░▒▓█▓▒░\n");
    printf("░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░        ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░        ░▒▓█▓▒░           ░▒▓█▓▒░\n");
    printf("░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░        ░▒▓█▓▒░░▒▓█▓▒░    ░▒▓█▓▒░\n");
    printf("░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░  ░▒▓██████▓▒░   ░▒▓██████▓▒░  ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓█▓▒░░▒▓█▓▒░ ░▒▓████████▓▒░  ░▒▓██████▓▒░     ░▒▓█▓▒░\n");                                                                                                                         
}

void clear_screen() {
    // Clear UART Screen
    printf("%c%c%c%c",0x1B,0x5B,0x32,0x4A);
}

void update_LED() {
    if (mode == 1) {
        if (lives == 3) {
            // Set the color to green at half intensity
            put_pixel(urgb_u32(0x00, 0x7F, 0x00));
        } else if (lives == 2) {
            // Set the color to blue at half intensity
            put_pixel(urgb_u32(0x00, 0x00, 0x7F));
        } else if (lives == 1) {
            // Set the color to orange at half intensity
            put_pixel(urgb_u32(0x7F, 0x52, 0x00));
        } else {
            // Set the color to red at half intensity
            put_pixel(urgb_u32(0x7F, 0x00, 0x00));
        }
    } else {
        // Set the led off
        put_pixel(urgb_u32(0x00, 0x00, 0x00));
    }
}

// Level 3 & 4 variables
//char words[20][20] = {"HI\0", "DOG\0", "ICE\0", "WOW\0", "DAY\0"};
char words[600][6] = {"ABA\0", "ABS\0", "ACE\0", "ACT\0", "ADD\0", "ADO\0", "AFT\0", "AGE\0", "AGO\0", "AHA\0", "AID\0", "AIM\0", "AIR\0", "ALA\0", "ALE\0", "ALL\0", "ALT\0", "AMP\0", "ANA\0", "AND\0", "ANT\0", "ANY\0", "APE\0", "APP\0", "APT\0", "ARC\0", "ARE\0", "ARK\0", "ARM\0", "ART\0", "ASH\0", "ASK\0", "ASP\0", "ASS\0", "ATE\0", "AVE\0", "AWE\0", "AXE\0", "AYE\0", "BAA\0", "BAD\0", "BAG\0", "BAN\0", "BAR\0", "BAT\0", "BAY\0", "BED\0", "BEE\0", "BEG\0", "BEL\0", "BEN\0", "BET\0", "BID\0", "BIG\0", "BIN\0", "BIO\0", "BIS\0", "BIT\0", "BIZ\0", "BOB\0", "BOG\0", "BOO\0", "BOW\0", "BOX\0", "BOY\0", "BRA\0", "BUD\0", "BUG\0", "BUM\0", "BUN\0", "BUS\0", "BUT\0", "BUY\0", "BYE\0", "CAB\0", "CAD\0", "CAM\0", "CAN\0", "CAP\0", "CAR\0", "CAT\0", "CHI\0", "COB\0", "COD\0", "COL\0", "CON\0", "COO\0", "COP\0", "COR\0", "COS\0", "COT\0", "COW\0", "COX\0", "COY\0", "CRY\0", "CUB\0", "CUE\0", "CUM\0", "CUP\0", "CUT\0", "DAB\0", "DAD\0", "DAL\0", "DAM\0", "DAN\0", "DAY\0", "DEE\0", "DEF\0", "DEL\0", "DEN\0", "DEW\0", "DID\0", "DIE\0", "DIG\0", "DIM\0", "DIN\0", "DIP\0", "DIS\0", "DOC\0", "DOE\0", "DOG\0", "DON\0", "DOT\0", "DRY\0", "DUB\0", "DUE\0", "DUG\0", "DUN\0", "DUO\0", "DYE\0", "EAR\0", "EAT\0", "EBB\0", "ECU\0", "EFT\0", "EGG\0", "EGO\0", "ELF\0", "ELM\0", "EMU\0", "END\0", "ERA\0", "ETA\0", "EVE\0", "EYE\0", "FAB\0", "FAD\0", "FAN\0", "FAR\0", "FAT\0", "FAX\0", "FAY\0", "FED\0", "FEE\0", "FEN\0", "FEW\0", "FIG\0", "FIN\0", "FIR\0", "FIT\0", "FIX\0", "FLU\0", "FLY\0", "FOE\0", "FOG\0", "FOR\0", "FOX\0", "FRY\0", "FUN\0", "FUR\0", "GAG\0", "GAL\0", "GAP\0", "GAS\0", "GAY\0", "GEE\0", "GEL\0", "GEM\0", "GET\0", "GIG\0", "GIN\0", "GOD\0", "GOT\0", "GUM\0", "GUN\0", "GUT\0", "GUY\0", "GYM\0", "HAD\0", "HAM\0", "HAS\0", "HAT\0", "HAY\0", "HEM\0", "HEN\0", "HER\0", "HEY\0", "HID\0", "HIM\0", "HIP\0", "HIS\0", "HIT\0", "HOG\0", "HON\0", "HOP\0", "HOT\0", "HOW\0", "HUB\0", "HUE\0", "HUG\0", "HUH\0", "HUM\0", "HUT\0", "ICE\0", "ICY\0", "IGG\0", "ILL\0", "IMP\0", "INK\0", "INN\0", "ION\0", "ITS\0", "IVY\0", "JAM\0", "JAR\0", "JAW\0", "JAY\0", "JET\0", "JEW\0", "JOB\0", "JOE\0", "JOG\0", "JOY\0", "JUG\0", "JUN\0", "KAY\0", "KEN\0", "KEY\0", "KID\0", "KIN\0", "KIT\0", "LAB\0", "LAC\0", "LAD\0", "LAG\0", "LAM\0", "LAP\0", "LAW\0", "LAX\0", "LAY\0", "LEA\0", "LED\0", "LEE\0", "LEG\0", "LES\0", "LET\0", "LIB\0", "LID\0", "LIE\0", "LIP\0", "LIT\0", "LOG\0", "LOT\0", "LOW\0", "MAC\0", "MAD\0", "MAG\0", "MAN\0", "MAP\0", "MAR\0", "MAS\0", "MAT\0", "MAX\0", "MAY\0", "MED\0", "MEG\0", "MEN\0", "MET\0", "MID\0", "MIL\0", "MIX\0", "MOB\0", "MOD\0", "MOL\0", "MOM\0", "MON\0", "MOP\0", "MOT\0", "MUD\0", "MUG\0", "MUM\0", "NAB\0", "NAH\0", "NAN\0", "NAP\0", "NAY\0", "NEB\0", "NEG\0", "NET\0", "NEW\0", "NIL\0", "NIP\0", "NOD\0", "NOR\0", "NOS\0", "NOT\0", "NOW\0", "NUN\0", "NUT\0", "OAK\0", "ODD\0", "OFF\0", "OFT\0", "OIL\0", "OLD\0", "OLE\0", "ONE\0", "OOH\0", "OPT\0", "ORB\0", "ORE\0", "OUR\0", "OUT\0", "OWE\0", "OWL\0", "OWN\0", "PAC\0", "PAD\0", "PAL\0", "PAM\0", "PAN\0", "PAP\0", "PAR\0", "PAS\0", "PAT\0", "PAW\0", "PAY\0", "PEA\0", "PEG\0", "PEN\0", "PEP\0", "PER\0", "PET\0", "PEW\0", "PHI\0", "PIC\0", "PIE\0", "PIG\0", "PIN\0", "PIP\0", "PIT\0", "PLY\0", "POD\0", "POL\0", "POP\0", "POT\0", "PRO\0", "PSI\0", "PUB\0", "PUP\0", "PUT\0", "RAD\0", "RAG\0", "RAJ\0", "RAM\0", "RAN\0", "RAP\0", "RAT\0", "RAW\0", "RAY\0", "RED\0", "REF\0", "REG\0", "REM\0", "REP\0", "REV\0", "RIB\0", "RID\0", "RIG\0", "RIM\0", "RIP\0", "ROB\0", "ROD\0", "ROE\0", "ROT\0", "ROW\0", "RUB\0", "RUE\0", "RUG\0", "RUM\0", "RUN\0", "RYE\0", "SAB\0", "SAC\0", "SAD\0", "SAE\0", "SAG\0", "SAL\0", "SAP\0", "SAT\0", "SAW\0", "SAY\0", "SEA\0", "SEC\0", "SEE\0", "SEN\0", "SET\0", "SEW\0", "SEX\0", "SHE\0", "SHY\0", "SIC\0", "SIM\0", "SIN\0", "SIP\0", "SIR\0", "SIS\0", "SIT\0", "SIX\0", "SKI\0", "SKY\0", "SLY\0", "SOD\0", "SOL\0", "SON\0", "SOW\0", "SOY\0", "SPA\0", "SPY\0", "SUB\0", "SUE\0", "SUM\0", "SUN\0", "SUP\0", "TAB\0", "TAD\0", "TAG\0", "TAM\0", "TAN\0", "TAP\0", "TAR\0", "TAT\0", "TAX\0", "TEA\0", "TED\0", "TEE\0", "TEN\0", "THE\0", "THY\0", "TIE\0", "TIN\0", "TIP\0", "TOD\0", "TOE\0", "TOM\0", "TON\0", "TOO\0", "TOP\0", "TOR\0", "TOT\0", "TOW\0", "TOY\0", "TRY\0", "TUB\0", "TUG\0", "TWO\0", "USE\0", "VAN\0", "VAT\0", "VET\0", "VIA\0", "VIE\0", "VOW\0", "WAN\0", "WAR\0", "WAS\0", "WAX\0", "WAY\0", "WEB\0", "WED\0", "WEE\0", "WET\0", "WHO\0", "WHY\0", "WIG\0", "WIN\0", "WIS\0", "WIT\0", "WON\0", "WOO\0", "WOW\0", "WRY\0", "WYE\0", "YEN\0", "YEP\0", "YES\0", "YET\0", "YOU\0", "ZIP\0", "ZOO\0"};
char morse[600][20] = {".- -... .-\0", ".- -... ...\0", ".- -.-. .\0", ".- -.-. -\0", ".- -.. -..\0", ".- -.. ---\0", ".- ..-. -\0", ".- --. .\0", ".- --. ---\0", ".- .... .-\0", ".- .. -..\0", ".- .. --\0", ".- .. .-.\0", ".- .-.. .-\0", ".- .-.. .\0", ".- .-.. .-..\0", ".- .-.. -\0", ".- -- .--.\0", ".- -. .-\0", ".- -. -..\0", ".- -. -\0", ".- -. -.--\0", ".- .--. .\0", ".- .--. .--.\0", ".- .--. -\0", ".- .-. -.-.\0", ".- .-. .\0", ".- .-. -.-\0", ".- .-. --\0", ".- .-. -\0", ".- ... ....\0", ".- ... -.-\0", ".- ... .--.\0", ".- ... ...\0", ".- - .\0", ".- ...- .\0", ".- .-- .\0", ".- -..- .\0", ".- -.-- .\0", "-... .- .-\0", "-... .- -..\0", "-... .- --.\0", "-... .- -.\0", "-... .- .-.\0", "-... .- -\0", "-... .- -.--\0", "-... . -..\0", "-... . .\0", "-... . --.\0", "-... . .-..\0", "-... . -.\0", "-... . -\0", "-... .. -..\0", "-... .. --.\0", "-... .. -.\0", "-... .. ---\0", "-... .. ...\0", "-... .. -\0", "-... .. --..\0", "-... --- -...\0", "-... --- --.\0", "-... --- ---\0", "-... --- .--\0", "-... --- -..-\0", "-... --- -.--\0", "-... .-. .-\0", "-... ..- -..\0", "-... ..- --.\0", "-... ..- --\0", "-... ..- -.\0", "-... ..- ...\0", "-... ..- -\0", "-... ..- -.--\0", "-... -.-- .\0", "-.-. .- -...\0", "-.-. .- -..\0", "-.-. .- --\0", "-.-. .- -.\0", "-.-. .- .--.\0", "-.-. .- .-.\0", "-.-. .- -\0", "-.-. .... ..\0", "-.-. --- -...\0", "-.-. --- -..\0", "-.-. --- .-..\0", "-.-. --- -.\0", "-.-. --- ---\0", "-.-. --- .--.\0", "-.-. --- .-.\0", "-.-. --- ...\0", "-.-. --- -\0", "-.-. --- .--\0", "-.-. --- -..-\0", "-.-. --- -.--\0", "-.-. .-. -.--\0", "-.-. ..- -...\0", "-.-. ..- .\0", "-.-. ..- --\0", "-.-. ..- .--.\0", "-.-. ..- -\0", "-.. .- -...\0", "-.. .- -..\0", "-.. .- .-..\0", "-.. .- --\0", "-.. .- -.\0", "-.. .- -.--\0", "-.. . .\0", "-.. . ..-.\0", "-.. . .-..\0", "-.. . -.\0", "-.. . .--\0", "-.. .. -..\0", "-.. .. .\0", "-.. .. --.\0", "-.. .. --\0", "-.. .. -.\0", "-.. .. .--.\0", "-.. .. ...\0", "-.. --- -.-.\0", "-.. --- .\0", "-.. --- --.\0", "-.. --- -.\0", "-.. --- -\0", "-.. .-. -.--\0", "-.. ..- -...\0", "-.. ..- .\0", "-.. ..- --.\0", "-.. ..- -.\0", "-.. ..- ---\0", "-.. -.-- .\0", ". .- .-.\0", ". .- -\0", ". -... -...\0", ". -.-. ..-\0", ". ..-. -\0", ". --. --.\0", ". --. ---\0", ". .-.. ..-.\0", ". .-.. --\0", ". -- ..-\0", ". -. -..\0", ". .-. .-\0", ". - .-\0", ". ...- .\0", ". -.-- .\0", "..-. .- -...\0", "..-. .- -..\0", "..-. .- -.\0", "..-. .- .-.\0", "..-. .- -\0", "..-. .- -..-\0", "..-. .- -.--\0", "..-. . -..\0", "..-. . .\0", "..-. . -.\0", "..-. . .--\0", "..-. .. --.\0", "..-. .. -.\0", "..-. .. .-.\0", "..-. .. -\0", "..-. .. -..-\0", "..-. .-.. ..-\0", "..-. .-.. -.--\0", "..-. --- .\0", "..-. --- --.\0", "..-. --- .-.\0", "..-. --- -..-\0", "..-. .-. -.--\0", "..-. ..- -.\0", "..-. ..- .-.\0", "--. .- --.\0", "--. .- .-..\0", "--. .- .--.\0", "--. .- ...\0", "--. .- -.--\0", "--. . .\0", "--. . .-..\0", "--. . --\0", "--. . -\0", "--. .. --.\0", "--. .. -.\0", "--. --- -..\0", "--. --- -\0", "--. ..- --\0", "--. ..- -.\0", "--. ..- -\0", "--. ..- -.--\0", "--. -.-- --\0", ".... .- -..\0", ".... .- --\0", ".... .- ...\0", ".... .- -\0", ".... .- -.--\0", ".... . --\0", ".... . -.\0", ".... . .-.\0", ".... . -.--\0", ".... .. -..\0", ".... .. --\0", ".... .. .--.\0", ".... .. ...\0", ".... .. -\0", ".... --- --.\0", ".... --- -.\0", ".... --- .--.\0", ".... --- -\0", ".... --- .--\0", ".... ..- -...\0", ".... ..- .\0", ".... ..- --.\0", ".... ..- ....\0", ".... ..- --\0", ".... ..- -\0", ".. -.-. .\0", ".. -.-. -.--\0", ".. --. --.\0", ".. .-.. .-..\0", ".. -- .--.\0", ".. -. -.-\0", ".. -. -.\0", ".. --- -.\0", ".. - ...\0", ".. ...- -.--\0", ".--- .- --\0", ".--- .- .-.\0", ".--- .- .--\0", ".--- .- -.--\0", ".--- . -\0", ".--- . .--\0", ".--- --- -...\0", ".--- --- .\0", ".--- --- --.\0", ".--- --- -.--\0", ".--- ..- --.\0", ".--- ..- -.\0", "-.- .- -.--\0", "-.- . -.\0", "-.- . -.--\0", "-.- .. -..\0", "-.- .. -.\0", "-.- .. -\0", ".-.. .- -...\0", ".-.. .- -.-.\0", ".-.. .- -..\0", ".-.. .- --.\0", ".-.. .- --\0", ".-.. .- .--.\0", ".-.. .- .--\0", ".-.. .- -..-\0", ".-.. .- -.--\0", ".-.. . .-\0", ".-.. . -..\0", ".-.. . .\0", ".-.. . --.\0", ".-.. . ...\0", ".-.. . -\0", ".-.. .. -...\0", ".-.. .. -..\0", ".-.. .. .\0", ".-.. .. .--.\0", ".-.. .. -\0", ".-.. --- --.\0", ".-.. --- -\0", ".-.. --- .--\0", "-- .- -.-.\0", "-- .- -..\0", "-- .- --.\0", "-- .- -.\0", "-- .- .--.\0", "-- .- .-.\0", "-- .- ...\0", "-- .- -\0", "-- .- -..-\0", "-- .- -.--\0", "-- . -..\0", "-- . --.\0", "-- . -.\0", "-- . -\0", "-- .. -..\0", "-- .. .-..\0", "-- .. -..-\0", "-- --- -...\0", "-- --- -..\0", "-- --- .-..\0", "-- --- --\0", "-- --- -.\0", "-- --- .--.\0", "-- --- -\0", "-- ..- -..\0", "-- ..- --.\0", "-- ..- --\0", "-. .- -...\0", "-. .- ....\0", "-. .- -.\0", "-. .- .--.\0", "-. .- -.--\0", "-. . -...\0", "-. . --.\0", "-. . -\0", "-. . .--\0", "-. .. .-..\0", "-. .. .--.\0", "-. --- -..\0", "-. --- .-.\0", "-. --- ...\0", "-. --- -\0", "-. --- .--\0", "-. ..- -.\0", "-. ..- -\0", "--- .- -.-\0", "--- -.. -..\0", "--- ..-. ..-.\0", "--- ..-. -\0", "--- .. .-..\0", "--- .-.. -..\0", "--- .-.. .\0", "--- -. .\0", "--- --- ....\0", "--- .--. -\0", "--- .-. -...\0", "--- .-. .\0", "--- ..- .-.\0", "--- ..- -\0", "--- .-- .\0", "--- .-- .-..\0", "--- .-- -.\0", ".--. .- -.-.\0", ".--. .- -..\0", ".--. .- .-..\0", ".--. .- --\0", ".--. .- -.\0", ".--. .- .--.\0", ".--. .- .-.\0", ".--. .- ...\0", ".--. .- -\0", ".--. .- .--\0", ".--. .- -.--\0", ".--. . .-\0", ".--. . --.\0", ".--. . -.\0", ".--. . .--.\0", ".--. . .-.\0", ".--. . -\0", ".--. . .--\0", ".--. .... ..\0", ".--. .. -.-.\0", ".--. .. .\0", ".--. .. --.\0", ".--. .. -.\0", ".--. .. .--.\0", ".--. .. -\0", ".--. .-.. -.--\0", ".--. --- -..\0", ".--. --- .-..\0", ".--. --- .--.\0", ".--. --- -\0", ".--. .-. ---\0", ".--. ... ..\0", ".--. ..- -...\0", ".--. ..- .--.\0", ".--. ..- -\0", ".-. .- -..\0", ".-. .- --.\0", ".-. .- .---\0", ".-. .- --\0", ".-. .- -.\0", ".-. .- .--.\0", ".-. .- -\0", ".-. .- .--\0", ".-. .- -.--\0", ".-. . -..\0", ".-. . ..-.\0", ".-. . --.\0", ".-. . --\0", ".-. . .--.\0", ".-. . ...-\0", ".-. .. -...\0", ".-. .. -..\0", ".-. .. --.\0", ".-. .. --\0", ".-. .. .--.\0", ".-. --- -...\0", ".-. --- -..\0", ".-. --- .\0", ".-. --- -\0", ".-. --- .--\0", ".-. ..- -...\0", ".-. ..- .\0", ".-. ..- --.\0", ".-. ..- --\0", ".-. ..- -.\0", ".-. -.-- .\0", "... .- -...\0", "... .- -.-.\0", "... .- -..\0", "... .- .\0", "... .- --.\0", "... .- .-..\0", "... .- .--.\0", "... .- -\0", "... .- .--\0", "... .- -.--\0", "... . .-\0", "... . -.-.\0", "... . .\0", "... . -.\0", "... . -\0", "... . .--\0", "... . -..-\0", "... .... .\0", "... .... -.--\0", "... .. -.-.\0", "... .. --\0", "... .. -.\0", "... .. .--.\0", "... .. .-.\0", "... .. ...\0", "... .. -\0", "... .. -..-\0", "... -.- ..\0", "... -.- -.--\0", "... .-.. -.--\0", "... --- -..\0", "... --- .-..\0", "... --- -.\0", "... --- .--\0", "... --- -.--\0", "... .--. .-\0", "... .--. -.--\0", "... ..- -...\0", "... ..- .\0", "... ..- --\0", "... ..- -.\0", "... ..- .--.\0", "- .- -...\0", "- .- -..\0", "- .- --.\0", "- .- --\0", "- .- -.\0", "- .- .--.\0", "- .- .-.\0", "- .- -\0", "- .- -..-\0", "- . .-\0", "- . -..\0", "- . .\0", "- . -.\0", "- .... .\0", "- .... -.--\0", "- .. .\0", "- .. -.\0", "- .. .--.\0", "- --- -..\0", "- --- .\0", "- --- --\0", "- --- -.\0", "- --- ---\0", "- --- .--.\0", "- --- .-.\0", "- --- -\0", "- --- .--\0", "- --- -.--\0", "- .-. -.--\0", "- ..- -...\0", "- ..- --.\0", "- .-- ---\0", "..- ... .\0", "...- .- -.\0", "...- .- -\0", "...- . -\0", "...- .. .-\0", "...- .. .\0", "...- --- .--\0", ".-- .- -.\0", ".-- .- .-.\0", ".-- .- ...\0", ".-- .- -..-\0", ".-- .- -.--\0", ".-- . -...\0", ".-- . -..\0", ".-- . .\0", ".-- . -\0", ".-- .... ---\0", ".-- .... -.--\0", ".-- .. --.\0", ".-- .. -.\0", ".-- .. ...\0", ".-- .. -\0", ".-- --- -.\0", ".-- --- ---\0", ".-- --- .--\0", ".-- .-. -.--\0", ".-- -.-- .\0", "-.-- . -.\0", "-.-- . .--.\0", "-.-- . ...\0", "-.-- . -\0", "-.-- --- ..-\0", "--.. .. .--.\0", "--.. --- ---\0"};

//char morse[20][20] = {".... ..\0", "-.. --- --.\0", ".. -.-. .\0", ".-- --- .--\0", "-.. .- -.--\0"};

void choose_expected () {
    // Generate New Question
    srand(time(0));
    if (level > 2) rand_num = rand() % 500;
    else rand_num = rand() % 36;
}

void stats () {
    // Print Statistics at the end of the level
    float accuracy = correct;
    accuracy *= 100.0;
    accuracy /= attempts;
    printf("█▓▒░ This level you had %d correct answer and %d incorrect answers.\n", correct, incorrect);
    printf("█▓▒░ Your overall accuracy was ");
    printf("%d", (int)accuracy);
    printf(".");
    accuracy = ((int)(accuracy)*10000) % 10000;
    printf("%d", (int)accuracy);
    printf("%% this level.\n█▓▒░\n");
}

void print_expected () {
    // Print new instructions
    printf("█▓▒░ Your so far is %d correct sequences in a row\n█▓▒░ You need %d correct sequences in a row to win this level.\n", right_input, CONSECUTIVE_TO_WIN);
    printf("█▓▒░ You have %d lives remaining.\n", lives);
    printf("█▓▒░\n█▓▒░ Your %s is ", level > 2 ? "word" : "character");
    if (level > 2) printf("\'%s\'", words[rand_num]);
    else printf("\'%c\'", char_array[rand_num]);
    if (level % 2 == 1) printf(" and its morse code is \'%s\'\n", level > 2 ? morse[rand_num] : morse_table[rand_num]);
    else printf(".\n");
}

void level_init(int n) {
    // Print Level Intro
    printf("█▓▒░ LEVEL-0%d\n", n);
    level = n;
    
    // Set first question
    reset_game_params();
    update_LED();
    choose_expected();
    print_expected();
    lower_edge();
}

void state_processor (int size) {
    /*
        We can't have while loops,
        unless we use multiple cores
        working at the same time, as
        this will block the rest of 
        the pico logic.

        So instead, we can use function
        calls and a bunch of global
        variables.
    */
    switch (mode) {
        /*
            In Mode 0, any morse input
            is taken as level select
            input.
        */
        case 0: {
            /*
                Check if input is numeric,
                we want 1-4 for the levels
            */

            // Is it the right length?
            if (0 < size && size <= 2) {
                // Is it in the right Hex range? 1-4
                if (0x30 < input[0] && input[0] <= 0x34) {
                    clear_screen();
                    mode = 1;

                    // Call Starter Function for the corresponding Level
                    switch ((int)(input[0] - 0x30)) {
                        case 1: {
                            level_init(1);
                            break;
                        }

                        case 2: {
                            level_init(2);
                            break;
                        }

                        case 3: {
                            level_init(3);
                            break;
                        }

                        case 4: {
                            level_init(4);
                            break;
                        }

                        default:{
                            // Print Error
                            printf("Input Error\n\n");
                            level = 0;
                            break;
                        }
                    }
                } else {
                    // Print Error
                    printf("Make sure you enter a value between 1 and 4.\n\n");
                }
            } else {
                // Print Error
                printf("Expecting a single digit.\n\n");
            }

            //sleep_ms(1000);

            break;
        }

        /*
            In Mode 1, any morse input
            is taken as Game input.
        */
        case 1: {
            // Compare input against expected input
            if (lives != 0) {
                clear_screen();
                attempts++;
                
                // Go character by character
                bool passed = true;
                if (level > 2) {
                    printf("Level: %d\n", level);
                    for (int j = 0; j < 10; j++) {
                        passed = true;
                        if (input[j] != words[rand_num][j]) {
                            passed = false;
                            break;
                        }

                        // End early if reached the end of the expected input
                        if (words[rand_num][j] == '\0') {
                            break;
                        } 
                    }
                } else {
                    if (size > 2) {
                        printf("Size: %d\n", size);
                        passed = false;
                    }

                    if (input[0] != char_array[rand_num]) {
                        printf("Got %c, expected %c.\n", input[0], char_array[rand_num]);
                        passed = false;
                    }
                }

                // Check if passed test
                if (passed) {
                    right_input++;
                    correct++;

                    // Check is has Max lives
                    if (lives < MAX_LIVES) {
                        lives++;

                        // Update LED Colour
                        update_LED();
                    }

                    correct_screen();

                    // Check if won
                    if (right_input >= CONSECUTIVE_TO_WIN) {
                        levelsCompleted[level - 1] = 1; // Mark level as completed

                        // Check if you won the game
                        bool gameWon = levelsCompleted[0] && levelsCompleted[1] && levelsCompleted[2] && levelsCompleted[3];
                        /* Condition to win the game, e.g., reaching a specific score or completing all levels */
                        if (gameWon) end_screen();
                        else level_complete_screen();

                        break;
                    } else {
                        // Set next question
                        upper_edge();
                        choose_expected();
                        print_expected();
                        lower_edge();
                    }
                } else {
                    incorrect_screen();

                    lives--;
                    right_input = 0;
                    incorrect++;

                    // Update LED Colour
                    update_LED();

                    // After each attempt, win or lose, show remaining lives
                    upper_edge();
                    printf("█▓▒░ You have %d %s remaining.\n", lives, lives == 1 ? "life" : "lives");
                    lower_edge();

                    if (lives <= 0) {
                        losing_screen();
                    } else {
                        upper_edge();
                        print_expected();
                        lower_edge();
                    }
                }
            }

            break;
        }
    }
}

/* ---FUNCTIONS--- */

// Must declare the main assembly entry point before use.
void main_asm();

// Initialise a GPIO pin – see SDK for detail on gpio_init()
void asm_gpio_init(uint pin) {
    gpio_init(pin);
}

// Set direction of a GPIO pin – see SDK for detail on gpio_set_dir()
void asm_gpio_set_dir(uint pin, bool out) {
    gpio_set_dir(pin, out);
}

// Get the value of a GPIO pin – see SDK for detail on gpio_get()
bool asm_gpio_get(uint pin) {
    return gpio_get(pin);
}

// Set the value of a GPIO pin – see SDK for detail on gpio_put()
void asm_gpio_put(uint pin, bool value) {
    gpio_put(pin, value);
}


// Enable edge interrupts – see SDK for detail on gpio_set_irq_enabled()
void asm_gpio_set_irq(uint pin) {
    gpio_set_irq_enabled(pin, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
}




/*

    Damo GPIO Interrupt function, Used to generate the user input
    for later in-game checks and comparisions.

*/

// Function Call from ASM to add a Dot to the input buffer
void add_dot () {
    // 0x2E is the Hex for the dot character in ASCII
    if (morse_index < MAX_MORSE_INPUT - 2) {
        morse_input[morse_index] = 0x2E;
        if (input_index == 0 && morse_index == 0) printf("> ");
        morse_index++;
        printf("%c", 0x2E);
    }
}


// Function Call from ASM to add a Dash to the input buffer
void add_dash () {
    // 0x2D is the Hex for the dash character in ASCII
    if (morse_index < MAX_MORSE_INPUT - 2) {
        morse_input[morse_index] = 0x2D;
        if (input_index == 0 && morse_index == 0) printf("> ");
        morse_index++;
        printf("%c", 0x2D);
    }
}

// Function Call from ASM to end a morse sequence for a single character
void end_char () {
    if (morse_index < MAX_MORSE_INPUT - 1) {
        // Add NULL terminator to morse input string
        morse_input[morse_index] = 0x0;

        // Reset Morse Input index        
        morse_index = 0;

        // Add character to input
        add_char();
    }

    printf("%c", 0x20);
}


// Function Call from ASM to end sequence and compare user input to the expected game input
void end_sequence () {
    // Add NULL terminator to input string
    if (input_index < MAX_INPUT - 1) {
        input[input_index] = 0x0;
        input_index++;
    }

    printf(":= %s\n", input);

    // Force State Processor to act on the input
    state_processor(input_index);

    // Reset Indexes
    input_index = 0;
    morse_index = 0;
}

void add_char () {
    bool passed = true;

    // Go through morse table to find our character
    for (int i = 0; i < 36; i++) {
        passed = true;
        
        // Go character by character
        for (int j = 0; j < 6; j++) {
            if (morse_input[j] != morse_table[i][j]) {
                passed = false;
                break;
            }

            if (morse_input[j] == '\0' || morse_table[i][j] == '\0') break;
        }

        if (passed) {
            // If found the character, add to the input
            if (morse_index < MAX_INPUT - 2) {
                input[input_index] = char_array[i];
                input_index++;
            }

            return;
        }
    }

    // If we got here, it means we haven't found our character, so add a '?' (0x3F in Hex).
    if (morse_index < MAX_INPUT - 2) {
        input[input_index] = 0x3F;
        input_index++;
    }

    return;
}

/*
    Main entry point for the code - simply calls the main assembly function.
*/
int main() {
    stdio_init_all();              // Initialise all basic IO

    // Initialise the PIO interface with the WS2812 code
    PIO pio = pio0;
    uint offset = pio_add_program(pio, &ws2812_program);
    ws2812_program_init(pio, 0, offset, WS2812_PIN, 800000, IS_RGBW);
    wd_enable();

    main_asm();
    return 0;
}