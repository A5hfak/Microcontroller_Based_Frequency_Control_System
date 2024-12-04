#include <util/delay.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdio.h>
#include <stdlib.h>  // For atoi()

#define F_CPU 16000000UL  // 16 MHz clock speed
#define BAUD 9600
#define MYUBRR F_CPU/16/BAUD-1

#define RXD_PIN PD0
#define TXD_PIN PD1
#define SOUNDER_PIN PB2
#define LED_PIN PD6  // Use PD6 for PWM with Timer0
#define POTENTIOMETER_PIN PC1
#define SWITCH_PIN PC2    // Define pin for the on/off switch

void LED_PWM_Init() {
	DDRD |= (1 << LED_PIN);  // Set PD6 as output for PWM
	TCCR0A = (1 << COM0A1) | (1 << WGM00) | (1 << WGM01);  // Fast PWM, non-inverting
	TCCR0B = (1 << CS01);  // Prescaler = 8
}

void USART_Init(unsigned int ubrr) {
	UBRR0H = (unsigned char)(ubrr >> 8);  // Set baud rate high byte
	UBRR0L = (unsigned char)ubrr;          // Set baud rate low byte
	UCSR0B = (1 << RXEN0) | (1 << TXEN0);  // Enable RX and TX
	UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);  // 8 data bits, 1 stop bit
}

void USART_Transmit(unsigned char data) {
	while (!(UCSR0A & (1 << UDRE0)));  // Wait until buffer is empty
	UDR0 = data;  // Transmit data
}

unsigned char USART_Receive(void) {
	while (!(UCSR0A & (1 << RXC0)));  // Wait until data is received
	return UDR0;  // Return received data
}

void USART_SendString(const char* str) {
	while (*str) {
		USART_Transmit(*str++);  // Transmit each character
	}
}

void ADC_Init() {
	ADMUX = (1 << REFS0);  // AVcc with external capacitor at AREF
	ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1);  // Enable ADC, Prescaler = 64
}

uint16_t read_potentiometer() {
	ADMUX = (ADMUX & 0xF0) | POTENTIOMETER_PIN;  // Select ADC channel PC1
	ADCSRA |= (1 << ADSC);  // Start conversion
	while (ADCSRA & (1 << ADSC));  // Wait for conversion to complete
	return ADC;  // Return ADC value
}

void sounder_init() {
	DDRB |= (1 << SOUNDER_PIN);  // Set PB2 as output
	TCCR1A = (1 << COM1B0);  // Toggle OC1B (PB2) on Compare Match
	TCCR1B = (1 << WGM12) | (1 << CS10);  // CTC mode, no prescaler
}

void set_sounder_frequency(uint16_t frequency) {
	OCR1A = (F_CPU / (2 * frequency)) - 1;  // Set the compare match value for Timer 1
}

void set_LED_brightness(uint8_t brightness) {
	OCR0A = brightness;  // Set LED brightness (0-255)
}

void enter_low_power_mode() {
	SMCR = (1 << SM1) | (1 << SE);  // Set sleep mode to power-save and enable sleep
	asm("sleep");  // Put the MCU to sleep
}

int main(void) {
	USART_Init(MYUBRR);  // Initialize USART
	ADC_Init();          // Initialize ADC
	LED_PWM_Init(); // Initialize LED PWM on PD6
	sounder_init();      // Initialize sounder

	DDRC &= ~(1 << SWITCH_PIN);  // Set SWITCH_PIN as input
	PORTC |= (1 << SWITCH_PIN);  // Enable pull-up resistor on SWITCH_PIN

	uint16_t potentiometer_value;
	uint16_t frequency = 50;  // Initial frequency
	char input_buffer[10] = {0};
	uint8_t input_index = 0;
	uint16_t print_counter = 0;  // For periodic printing
	char buffer[50];

	sei();  // Enable global interrupts

	while (1) {
		if (PINC & (1 << SWITCH_PIN)) {  // If switch is OFF (high state)
			enter_low_power_mode();
			} else {  // If switch is ON (low state)
			if (print_counter >= 2000) {  // Approximately every 2 seconds
				snprintf(buffer, sizeof(buffer), "\nFrequency is %u Hz\n", frequency);
				USART_SendString(buffer);
				print_counter = 0;  // Reset the counter
			}

			if (UCSR0A & (1 << RXC0)) {  // Check if data is received
				char command = USART_Receive();  // Receive character

				// Handle '+' and '-' commands for immediate frequency change
				if (command == '+' || command == '-') {
					// Update frequency when receiving '+' or '-'
					frequency += (command == '+') ? 10 : -10;

					// Ensure frequency stays within valid range (50-1000 Hz)
					if (frequency < 50) frequency = 50;
					if (frequency > 1000) frequency = 1000;

					// Print the updated frequency immediately after change
					snprintf(buffer, sizeof(buffer), "\nFrequency updated to: %u Hz\n", frequency);
					USART_SendString(buffer);

					} else if ((command >= '0' && command <= '9') || command == '\n' || command == '\r') {
					// If the command is a digit, append it to the input buffer
					if (command >= '0' && command <= '9') {
						input_buffer[input_index++] = command;
					}
					
					// When the Enter key is pressed
					if (command == '\n' || command == '\r') {
						input_buffer[input_index] = '\0';  // Null-terminate the string

						// Convert the input buffer to an integer (new frequency)
						uint16_t new_frequency = atoi(input_buffer);

						// Print the received value of new_frequency before checking the range
						snprintf(buffer, sizeof(buffer), "\nReceived frequency: %u Hz\n", new_frequency);
						USART_SendString(buffer);  // Print the new_frequency value

						// Check if the new frequency is within the valid range
						if (new_frequency >= 50 && new_frequency <= 1000) {
							frequency = new_frequency;  // Set the frequency to the input value
							USART_SendString("\nFrequency set to: ");
							snprintf(buffer, sizeof(buffer), "%u Hz\n", new_frequency);  // Print the new frequency
							USART_SendString(buffer);
							} else {
							USART_SendString("\nInvalid frequency. Enter a value between 50 and 1000.\n");
						}

						// Reset input buffer for the next input
						input_index = 0;
					}
				}
			}


			potentiometer_value = read_potentiometer();
			uint16_t pot_frequency = 50 + ((potentiometer_value * 950UL) / 1023);
			frequency = pot_frequency;

			set_sounder_frequency(frequency);  // Set the sounder frequency
			set_LED_brightness((frequency / 4) % 256); // Control brightness of LED on PD6 using hardware PWM
			
			_delay_ms(1);
			print_counter++;
		}
	}
}