#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define RED_LED PORTC3
#define YELLOW_LED PORTC4
#define GREEN_LED PORTC5
#define BUTTON0 PORTD7
#define BUTTON1 PORTD6
#define BUTTON2 PORTD5
#define MAX_PATTERN_LENGTH 15
#define MIN_PATTERN_LENGTH 3

// Decrease 2 bit vertical counter where mask = 1.
// Set counters to binary 11 where mask = 0.
#define VC_DEC_OR_SET(high, low, mask) \
  low = ~(low & mask);                 \
  high = low ^ (high & mask);

static void init_gpio(void);
static inline void debounce(void);
// Return non-zero if a button matching mask is pressed.
uint8_t button_down(uint8_t button_mask);
// Used for debouncing
static void enable_timer0(void);
static void enable_timer1(void);
static void clear_leds(void);
static void get_random_pattern(uint8_t *pattern_arr, const uint8_t arr_length);
static inline uint8_t next_pattern_part(void);
// Read a single pattern part (one led).
// Returns selected led index or -1 if no button was clicked.
static int8_t get_user_pattern_part(void);
static void record_pattern(void);
static inline void blink_selected_led(const uint8_t led_index);
static uint8_t check_game_outcome(void);

enum game_state
{
  STARTING,
  SHOWING_PATTERN,
  RECORDING_PATTERN,
  BLINKING_SELECTED_LED,
  GAME_OVER
};

// The pattern is initialized during the main procedure
uint8_t pattern[MAX_PATTERN_LENGTH];
uint8_t user_pattern[MAX_PATTERN_LENGTH];
uint8_t pattern_length = MIN_PATTERN_LENGTH;
int8_t pattern_index = 0;
volatile enum game_state gstate = STARTING;
// Variable to tell that the button is pressed (and debounced).
// Can be read with button_down() which will clear it.
volatile uint8_t buttons_down;

// Timer1 interrupt routine to handle time specific game logic
ISR(TIMER1_COMPA_vect)
{
  // Zero counter value
  TCNT1 = 0;

  switch (gstate)
  {
  case STARTING:
    // Blink all leds
    PORTC ^= (1 << GREEN_LED);
    PORTC ^= (1 << YELLOW_LED);
  case GAME_OVER:
    // Blink red led only
    PORTC ^= (1 << RED_LED);
    break;
  case SHOWING_PATTERN:
    if (next_pattern_part() == 1)
    {
      pattern_index = 0;
      gstate = RECORDING_PATTERN;
    }
    break;
  case BLINKING_SELECTED_LED:
    blink_selected_led(user_pattern[pattern_index]);
    buttons_down = 0;
    break;
  }
}

// Timer0 interrupt routine to handle 10 ms interval buttons debouncing
ISR(TIMER0_COMPA_vect)
{
  TCNT0 = 0;
  debounce();
}

static uint8_t check_game_outcome(void)
{
  for (uint8_t i = 0; i < pattern_length; i++)
    if (pattern[i] != user_pattern[i])
      return 1;

  return 0;
}

static inline void blink_selected_led(const uint8_t led_index)
{
  static uint8_t blink_counter = 0;

  if (blink_counter == 2)
  {
    pattern_index++;
    blink_counter = 0;
    gstate = RECORDING_PATTERN;
  }
  else
  {
    switch (led_index)
    {
    case 0:
      PORTC ^= (1 << GREEN_LED);
      break;
    case 1:
      PORTC ^= (1 << YELLOW_LED);
      break;
    case 2:
      PORTC ^= (1 << RED_LED);
      break;
    }

    blink_counter++;
  }
}

static int8_t get_user_pattern_part(void)
{
  if (button_down(1 << BUTTON0))
    return 0;
  else if (button_down(1 << BUTTON1))
    return 1;
  else if (button_down(1 << BUTTON2))
    return 2;

  return -1;
}

// Check button state and set bits in the button_down variable if a
// debounced button down press is detected.
// Call this function every 10 ms or so.
static inline void debounce(void)
{
  // Eight vertical two bit counters for number of equal states.
  static uint8_t vcount_low = 0xFF, vcount_high = 0xFF;
  // Keep track of current (debounced) state
  static uint8_t button_state = 0;
  // Read buttons (active low). Xor with button_state
  // to see which ones are about to change state.
  uint8_t state_changed = ~(PIND ^ button_state);
  // Decrease counters where state_changed = 1, set the others to 0b11
  VC_DEC_OR_SET(vcount_high, vcount_low, state_changed);
  // Update state_changed to have a 1 only if the counter overflowed
  state_changed &= vcount_low & vcount_high;
  // Change button_state for the buttons who's counters rolled over
  button_state ^= state_changed;
  // Update button_down with buttons who's counters rolled over
  // and who's state is 1 (pressed).
  buttons_down |= button_state & state_changed;
}

uint8_t button_down(uint8_t button_mask)
{
  // ATOMIC_BLOCK is needed if debounce() is called from within an ISR
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
  {
    // 'and' with debounced_state for a one if they match
    button_mask &= buttons_down;
    // Clear if there was a match
    buttons_down ^= button_mask;
  }
  // Return non-zero if there was a match
  return button_mask;
}

static void record_pattern(void)
{
  int8_t button_index = get_user_pattern_part();

  if (button_index != -1)
  {
    // Save selected led index
    user_pattern[pattern_index] = button_index;
    gstate = BLINKING_SELECTED_LED;
  }
}

// Fills an array with random pattern of 0, 1 or 2 at each index
// representing led port indexes (led0, led1, led2) to set
static void get_random_pattern(uint8_t *pattern_arr,
                               const uint8_t pattern_length)
{
  // Clear pattern_arr
  memset((uint8_t*)pattern_arr, 0, MAX_PATTERN_LENGTH * sizeof(uint8_t));

  for (uint8_t i = 0; i < pattern_length; i++)
  {
    pattern_arr[i] = rand() % 3;
  }
}

// Returns 1 if pattern_index > pattern_length - 1.
// Otherwise lights up another led according to the pattern and returns 0.
static inline uint8_t next_pattern_part(void)
{
  static uint8_t call_counter = 0;
  clear_leds();

  if (call_counter == UINT8_MAX)
    call_counter = 0;

  if (pattern_index < 0 || pattern_index > pattern_length - 1)
    return 1;
  // Only one of two calls of this function lights a led
  // to make led blinks differentiable for an user.
  else if ((call_counter++) % 2 == 0)
  {
    switch (pattern[pattern_index++])
    {
    case 0:
      PORTC |= (1 << GREEN_LED);
      break;

    case 1:
      PORTC |= (1 << YELLOW_LED);
      break;

    case 2:
      PORTC |= (1 << RED_LED);
      break;
    }
  }

  return 0;
}

static void clear_leds(void)
{
  PORTC &= ~(1 << RED_LED);
  PORTC &= ~(1 << YELLOW_LED);
  PORTC &= ~(1 << GREEN_LED);
}

static void enable_timer0(void)
{
  // Set prescaler to 64
  TCCR0B = (1 << CS01) | (1 << CS00);
  // Clear counter value
  TCNT0 = 0;
  // Compare interrupt value for 10 ms (actually 9.984ms ~ 100.16Hz)
  OCR0A = 155;
  // Set interrupt on TCNT0 == OCR0A
  TIMSK0 |= (1 << OCIE0A);
}

static void enable_timer1(void)
{
  // Set prescaler to 64
  TCCR1B |= (1 << CS11) | (1 << CS10);
  // Clear counter value
  TCNT1 = 0;
  // Compare interrupt value for 500 second
  OCR1A = 7812;
  // Set interrupt on TCNT1 == OCR1A
  TIMSK1 |= (1 << OCIE1A);
}

static void init_gpio(void)
{
  // LED ports as outputs
  DDRC = 0xFF;
  // Set pull up resistors
  PORTD |= (1 << BUTTON0) | (1 << BUTTON1) | (1 << BUTTON2);
}

int main(void)
{
  init_gpio();
  // Enable interrupts
  sei();
  enable_timer0();
  enable_timer1();

  while (1)
  {
    switch (gstate)
    {
      // Wait until a player is ready to play
    case STARTING:
    case GAME_OVER:
      if (button_down(1 << BUTTON2))
      {
        // Set seed based on current timer1 counter value
        srand((unsigned int)TCNT1);
        get_random_pattern(pattern, pattern_length);
        clear_leds();
        pattern_index = 0;
        // Standard blink length 500ms
        OCR1A = 7812;
        gstate = SHOWING_PATTERN;
      }
      break;
    case SHOWING_PATTERN:
      buttons_down = 0;
      // The functionality of this state is based entirely
      // on timer1 compA interrupt routine.
      break;
    case RECORDING_PATTERN:
      if (pattern_index > pattern_length - 1)
      {
        if (check_game_outcome())
        {
          pattern_length = MIN_PATTERN_LENGTH;
          gstate = GAME_OVER;
        }
        else
        {
          // Game won
          gstate = STARTING;

          if(pattern_length > MAX_PATTERN_LENGTH)
            pattern_length = MIN_PATTERN_LENGTH;
          else
            pattern_length++;
        }
      }
      else
      {
        // User clicks last only 250ms
        OCR1A = 3906;
        record_pattern();
      }
      break;
    }
  }

  return 0;
}