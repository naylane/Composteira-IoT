/* 
****************************************************
    Sistema de Monitoramento de Composteira
            Projeto Final EmbarcaTech

        Autora: Naylane do Nascimento Ribeiro
****************************************************
*/

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "hardware/pio.h"
#include "lib/ssd1306.h"
#include "lib/led.h"
#include "lib/WS2812.h"
#include "WS2812.pio.h"

#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define endereco 0x3C

#define WS2812_PIN 7

#define BTN_A 5                     // Pino do botão A conectado ao GPIO 5.
#define BTN_B 6                     // Pino do botão B conectado ao GPIO 6.
#define BTN_STICK 22                // Pino do botão do Joystick conectado ao GPIO 22.

#define DEBOUNCE_TIME 200000        // Tempo para debounce em ms
#define WAIT_TIME     1000000       // ...
uint32_t last_time = 0;             // Tempo da última interrupção do botão do Joystick
static uint32_t last_time_A = 0;    // Tempo da última interrupção do botão A
static uint32_t last_time_B = 0;    // Tempo da última interrupção do botão B

#define PWM_FREQ   20000            // 20 kHz
#define PWM_WRAP   255              // Valor do WRAP (período) para o PWM. 8 bits de wrap (256 valores)
const float DIVIDER_PWM = 125.0;    // Valor do divisor de clock para o PWM.

#define LED_R 13
#define LED_G 11
#define LED_B 12

#define BUZZER_PIN 10 // Pino do Buzzer conectado ao GPIO 10.

// --- VARIAVEIS GLOBAIS

ssd1306_t ssd;
int temperatura = 39;
int umidade = 49;
int oxigenio = 14;
PIO pio = pio0;
uint sm = 0;

// --- DECLARAÇÃO DE FUNÇÕES

void update_data(int *data, bool increase);
void write_display(ssd1306_t *ssd);
void irq_buttons(uint gpio, uint32_t events);
void beep_buzzer();
void buzzer_tone(int frequency);
void buzzer_off();
void setup();
void setup_display();
void setup_buzzer();
void setup_button(uint pin);
void setup_led(uint pin);


/**
 * @brief Função principal
*/
int main() {
    setup();

    clear_matrix(pio, sm);

    while (1) {
        write_display(&ssd);

        if ((temperatura > 60) || (umidade > 70) || (oxigenio < 15)) {
            set_led(LED_R, 1);
            set_led(LED_G, 0);
            set_led(LED_B, 0);

            set_led_matrix(11, pio, sm);

            buzzer_tone(50); 
            sleep_ms(500);
            buzzer_off();      
        }
        else if ((40 < temperatura && temperatura < 60) && (50 < umidade && umidade < 70) && (oxigenio > 15)) {
            set_led(LED_R, 0);
            set_led(LED_G, 1);
            set_led(LED_B, 0);
            set_led_matrix(15, pio, sm);
        }
        else {
            set_led(LED_R, 0);
            set_led(LED_G, 0);
            set_led(LED_B, 1);
            set_led_matrix(15, pio, sm);
        }

        sleep_ms(1000);
    }
}


/**
 * @brief Função de interrupção para os botões com debouncing.
 * 
 * @param gpio a GPIO que gerou interrupção.
 * @param events a evento que gerou interrupção.
 */
void irq_buttons(uint gpio, uint32_t events){
    uint32_t current_time = to_us_since_boot(get_absolute_time());
    
    if(current_time - last_time > DEBOUNCE_TIME){
        bool increase = (current_time - last_time) > WAIT_TIME;  // Se o tempo entre cliques for grande, aumenta, senão, diminui

        switch (gpio) {
        case BTN_A:
            update_data(&temperatura, increase);
            break;
        case BTN_B:
            update_data(&umidade, increase);
            break;
        case BTN_STICK:
            update_data(&oxigenio, increase);
            break;
        default:
            break;
        }

        last_time = current_time; // Atualiza o tempo para o debounce
    }
}


/**
 * @brief Simula a alteração de valores dos sensores.
 * 
 * @param data variável que irá ser alterada.
 * @param increase booleano que informa se é incremento ou decremento de valor.
 */
void update_data(int *data, bool increase) {
    if (increase) {
        *data += 5;
    } else {
        *data -= 5;
    }
}


/**
 * @brief Atualiza o display com o estado dos LEDs.
 *
 * @param ssd Ponteiro para a estrutura do display.
 */
void write_display(ssd1306_t *ssd) {
    // Limpa o display
    ssd1306_fill(ssd, false);

    char msg_temp[20];  // Buffer para armazenar a string formatada
    sprintf(msg_temp, "Temperatura %d", temperatura);
    ssd1306_draw_string(ssd, msg_temp, 0, 0);

    char msg_umid[20];  // Buffer para armazenar a string formatada
    sprintf(msg_umid, "Umidade %d", umidade);
    ssd1306_draw_string(ssd, msg_umid, 0, 15);

    char msg_oxig[20];  // Buffer para armazenar a string formatada
    sprintf(msg_oxig, "Oxigenio %d", oxigenio);
    ssd1306_draw_string(ssd, msg_oxig, 0, 30);

    // Atualiza o display
    ssd1306_send_data(ssd); 
}


/**
 * @brief Função para tocar um tom (frequência em Hz)
 */
void buzzer_tone(int frequency) {
    uint slice = pwm_gpio_to_slice_num(BUZZER_PIN);
    uint32_t wrap_value = clock_get_hz(clk_sys) / (DIVIDER_PWM * frequency);
    pwm_set_wrap(slice, wrap_value);
    pwm_set_chan_level(slice, PWM_CHAN_A, wrap_value / 2); // 50% do ciclo
}


/**
 * @brief Função para desligar o buzzer PWM.
 */
void buzzer_off() {
    pwm_set_chan_level(pwm_gpio_to_slice_num(BUZZER_PIN), PWM_CHAN_A, 0);
}


/**
 * @brief Inicialização e configuração geral.
*/
void setup() {
    // Inicializa entradas e saídas
    stdio_init_all();

    // Inicializa o PIO para controlar a matriz de LEDs (WS2812)
    PIO pio = pio0;
    uint sm = 0;
    uint offset = pio_add_program(pio, &pio_matrix_program);
    pio_matrix_program_init(pio, sm, offset, WS2812_PIN);

    // Aguarda a conexão USB
    sleep_ms(2000); 

    // Configura os LEDs RGB
    setup_led(LED_R);
    setup_led(LED_G);
    setup_led(LED_B);

    // Configura botões A e B e botão do joystick
    setup_button(BTN_A);
    setup_button(BTN_B);
    setup_button(BTN_STICK);

    // Configura Buzzer como saída PWM
    setup_buzzer();
    
    // Configura interrupção dos botões
    gpio_set_irq_enabled_with_callback(BTN_A, GPIO_IRQ_EDGE_RISE, true, &irq_buttons);
    gpio_set_irq_enabled_with_callback(BTN_B, GPIO_IRQ_EDGE_RISE, true, &irq_buttons);
    gpio_set_irq_enabled_with_callback(BTN_STICK, GPIO_IRQ_EDGE_FALL, true, &irq_buttons); 

    // Inicializa I2C com 400 Khz
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);                    // Set the GPIO pin function to I2C
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);                    // Set the GPIO pin function to I2C
    gpio_pull_up(I2C_SDA);                                        // Pull up the data line
    gpio_pull_up(I2C_SCL);                                        // Pull up the clock line

    // Configura display
    setup_display();
}


/**
 * @brief Configura Display ssd1306 via I2C, iniciando com todos os pixels desligados.
*/
void setup_display() {
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT); // Inicializa o display
    ssd1306_config(&ssd);    
    ssd1306_send_data(&ssd);   
    ssd1306_fill(&ssd, false);
}


/**
 * @brief Configura Buzzer como saída PWM.
*/
void setup_buzzer() {
    gpio_set_function(BUZZER_PIN, GPIO_FUNC_PWM); // Configura o pino para PWM
    uint slice = pwm_gpio_to_slice_num(BUZZER_PIN);
    pwm_set_wrap(slice, PWM_WRAP);  // Define a resolução do PWM
    pwm_set_clkdiv(slice, DIVIDER_PWM);  // Define o divisor do clock
    pwm_set_enabled(slice, true);  // Habilita PWM
}


/**
 * @brief Configura push button como saída e com pull-up.
 * 
 * @param pin o pino do push button.
 */
void setup_button(uint pin) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);
}
