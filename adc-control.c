#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/clocks.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "lib/ssd1306.h"
#include "lib/led.h"

#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define endereco 0x3C

#define BTN_A 5                     // Pino do botão A conectado ao GPIO 5.
#define BTN_B 6                     // Pino do botão B conectado ao GPIO 6.
#define BTN_STICK 22                // Pino do botão do Joystick conectado ao GPIO 22.

#define DEBOUNCE_TIME 200000        // Tempo para debounce em ms
#define WAIT_TIME     1000000       // ...
uint32_t last_time = 0;             // Armazena o tempo do último evento do botão (em microssegundos)

#define PWM_FREQ   20000            // 20 kHz
#define PWM_WRAP   255              // Valor do WRAP (período) para o PWM. 8 bits de wrap (256 valores)
const float DIVIDER_PWM = 125.0;    // Valor do divisor de clock para o PWM.

#define LED_R 13
#define LED_G 11
#define LED_B 12

// --- VARIAVEIS GLOBAIS

ssd1306_t ssd;
int temperatura = 40;
int umidade = 50;
int oxigenio = 15;

// --- DECLARAÇÃO DE FUNÇÕES

void update_data(int *data, bool increase);
void led_status_display(ssd1306_t *ssd);
void irq_buttons(uint gpio, uint32_t events);
void setup();
void setup_display();
void setup_button(uint pin);
void setup_led(uint pin);


/**
 * @brief Função principal
*/
int main() {
    setup();

    while (1) {
        led_status_display(&ssd);

        //printf("Temperatura: %d; Umidade: %d; Oxigenio: %d \n", temperatura, umidade, oxigenio);

        if ((temperatura > 60) || (umidade > 70) || (oxigenio < 15)) {
            set_led(LED_R, 1);
            set_led(LED_G, 0);
            set_led(LED_B, 0);
        }
        else if ((40 < temperatura && temperatura < 60) && (50 < umidade && umidade < 70) && (oxigenio > 15)) {
            set_led(LED_R, 0);
            set_led(LED_G, 1);
            set_led(LED_B, 0);
        }
        else {
            set_led(LED_R, 0);
            set_led(LED_G, 0);
            set_led(LED_B, 1);
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
void led_status_display(ssd1306_t *ssd) {
    // Limpa o display
    ssd1306_fill(ssd, false);

    char msg_temp[10];  // Buffer para armazenar a string formatada
    sprintf(msg_temp, "T %d", temperatura);
    ssd1306_draw_string(ssd, msg_temp, 0, 0);

    char msg_umid[10];  // Buffer para armazenar a string formatada
    sprintf(msg_umid, "U %d", umidade);
    ssd1306_draw_string(ssd, msg_umid, 0, 15);

    char msg_oxig[10];  // Buffer para armazenar a string formatada
    sprintf(msg_oxig, "O %d", oxigenio);
    ssd1306_draw_string(ssd, msg_oxig, 0, 30);

    bool alert = gpio_get(LED_R);
    char *msg_alert = alert ? "Alerta" : "Ok";
    ssd1306_draw_string(ssd, msg_alert, 0, 45);

    // Atualiza o display
    ssd1306_send_data(ssd); 
}


/**
 * @brief Inicialização e configuração geral.
*/
void setup() {
    // Inicializa entradas e saídas
    stdio_init_all();

    // Configura os LEDs RGB
    setup_led(LED_R);
    setup_led(LED_G);
    setup_led(LED_B);

    // Configura botões A e B e botão do joystick
    setup_button(BTN_A);
    setup_button(BTN_B);
    setup_button(BTN_STICK);
    
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
 * @brief Configura push button como saída e com pull-up.
 * 
 * @param pin o pino do push button.
 */
void setup_button(uint pin) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);
}
