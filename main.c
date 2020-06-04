#include <avr/io.h>
#include <util/delay.h>

#define RED_LED PORTB0
#define YELLOW_LED PORTD7
#define GREEN_LED PORTD6
#define PULLUP1 PORTC3
#define PULLUP2 PORTC4
#define PULLUP3 PORTC5
#define BUTTON1 PORTC3
#define BUTTON2 PORTC4
#define BUTTON3 PORTC5

#define CHECK_BUTTON(PIN, PORT, INDEX)                            \
  {                                                               \
    if ((PIN & (1 << PORT)) == 0)                                 \
    {                                                             \
      _delay_ms(10);                                              \
      if ((PIN & (1 << PORT)) == 0 && buttons_states[INDEX] == 0) \
      {                                                           \
        buttons_states[INDEX] = 1;                                \
      }                                                           \
    }                                                             \
    else                                                          \
    {                                                             \
      buttons_states[INDEX] = 0;                                  \
    }                                                             \
  }

#define TOGGLE_LED(PORT, LED, INDEX) \
  {                                  \
    if (buttons_states[INDEX] == 1)  \
    {                                \
      PORT |= (1 << LED);            \
    }                                \
    else                             \
    {                                \
      PORT &= ~(1 << LED);           \
    }                                \
  }

static void init_gpio(void);
static void manage_buttons(void);
static void manage_leds(void);

// Global array is initialized with 0s
char buttons_states[3];

static void init_gpio(void)
{
  // LED ports as outputs
  DDRB = 0x01;
  DDRD = 0x60;
  // Set pullup resistors
  PORTC |= (1 << PULLUP1) | (1 << PULLUP2) | (1 << PULLUP3);
}

static void manage_buttons(void)
{
  CHECK_BUTTON(PINC, BUTTON1, 0);
  CHECK_BUTTON(PINC, BUTTON2, 1);
  CHECK_BUTTON(PINC, BUTTON3, 2);
}

static void manage_leds(void)
{
  TOGGLE_LED(PORTB, RED_LED, 0);
  TOGGLE_LED(PORTD, YELLOW_LED, 1);
  TOGGLE_LED(PORTD, GREEN_LED, 2);
}

int main(void)
{
  init_gpio();

  while (1)
  {
    manage_buttons();
    manage_leds();
  }
}