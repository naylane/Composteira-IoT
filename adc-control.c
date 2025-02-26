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
bool cor = true;

#define BTN_A 5                     // Pino do botão A conectado ao GPIO 5.
#define BTN_B 6                     // Pino do botão B conectado ao GPIO 6.
#define BTN_STICK 22                // Pino do botão do Joystick conectado ao GPIO 22.

#define DEBOUNCE_TIME 200000        // Tempo para debounce em ms
uint32_t last_time = 0;             // Armazena o tempo do último evento do botão (em microssegundos)

#define PWM_FREQ   20000            // 20 kHz
#define PWM_WRAP   255              // Valor do WRAP (período) para o PWM. 8 bits de wrap (256 valores)
const float DIVIDER_PWM = 125.0;    // Valor do divisor de clock para o PWM.

#define LED_R 12
#define LED_G 11
#define LED_B 13

// --- VARIAVEIS GLOBAIS

ssd1306_t ssd;
volatile bool leds_pwm_state = 0; 
volatile bool led_green_state = 0;
volatile bool BUTTON_A_pressionado = false;

int pulso_x;
int pulso_y;
uint16_t adc_value_x;
uint16_t adc_value_y; 
uint slice_blue;
uint slice_red;

// --- DECLARAÇÃO DE FUNÇÕES

int le_dados();
void irq_buttons(uint gpio, uint32_t events);
void converte_joystic (int input);
void setup();
void setup_display();
void setup_pwm_leds();
void setup_button(uint pin);
void setup_led(uint pin);


/**
 * @brief Função principal
*/
int main() {
    setup();

    int x = 63, y = 31; // Valor central do ssd1306

    while (true) {
        converte_joystic(0);
        converte_joystic(1);
        
        pwm_set_gpio_level(LED_R, pulso_x);  
        pwm_set_gpio_level(LED_B, pulso_y); 
        
        ssd1306_fill(&ssd, !cor); // Limpa o display
        
        if(led_green_state) {
            ssd1306_rect(&ssd, 1, 1, 126, 62, cor, !cor); // Desenha um retângulo borda
        }

        // Eixo X
        if(adc_value_y == 2048){ // Se estiver no meio 
            x = 63 ;
        }
        else if (adc_value_y > 2048){ // Se aumentar os valores
            x = ((adc_value_y - 2048) / 32) + 54;
        }
        else{ // Se diminuir os valores
            x = (((adc_value_y - 2048)) / 32) + 67;
        }
        
        // Eixo Y
        if(adc_value_x == 2048)
            y = 31;
        else if(adc_value_x > 2048){
            y =  67 - (adc_value_x / 64);
            // y =  ((adc_value_x - 2048) / 64) + 24; PARA VALOR INVERTIDO
        }
        else{
            y = 54 - ((adc_value_x) / 64);
            // y =  ((adc_value_x - 2048) / 64) + 35; //PARA VALOR INVERTIDO
        }
        
        ssd1306_rect(&ssd, 3, 3, 122, 60, cor, !cor); // Desenha um retângulo
        ssd1306_rect(&ssd, y, x, 8, 8, cor, cor); // Desenha um retângulo        
        ssd1306_send_data(&ssd); // Atualiza o display

        sleep_ms(100);
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
        if (gpio == BTN_A) {
            // Habilita o PWM
            pwm_set_enabled(slice_red, leds_pwm_state);
            pwm_set_enabled(slice_blue, leds_pwm_state); 
            leds_pwm_state = !leds_pwm_state;
        }
        else if (gpio == BTN_B) {
            reset_usb_boot(0, 0); //func para entrar no modo bootsel 
        }        
        else if (gpio == BTN_STICK) {
            led_green_state = !led_green_state;
            gpio_put(LED_G, led_green_state);
        }
        last_time = current_time; // Atualiza o tempo para o debounce
    }
}


/**
 * @brief Inicialização e configuração geral.
*/
void setup() {
    // Inicializa entradas e saídas
    stdio_init_all();

    // Configura o LED Verde
    setup_led(LED_G);

    // Configura botões A e B e botão do joystick
    setup_button(BTN_A);
    setup_button(BTN_B);
    setup_button(BTN_STICK);
    
    // Configura interrupção dos botões
    gpio_set_irq_enabled_with_callback(BTN_A, GPIO_IRQ_EDGE_RISE, true, &irq_buttons);
    gpio_set_irq_enabled_with_callback(BTN_B, GPIO_IRQ_EDGE_RISE, true, &irq_buttons);
    gpio_set_irq_enabled_with_callback(BTN_STICK, GPIO_IRQ_EDGE_FALL, true, &irq_buttons); 
    
    // Inicia ADC
    adc_init();
    adc_gpio_init(EIXO_X);
    adc_gpio_init(EIXO_Y);

    // Inicializa o PWM para os LEDs Vermelho e Azul
    setup_pwm_leds();

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
 * @brief Configura Display ssd1306 via I2C.
*/
void setup_display() {
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT); // Inicializa o display
    ssd1306_config(&ssd);    
    ssd1306_send_data(&ssd);   

    // Limpa o display. O display inicia com todos os pixels desligados.
    ssd1306_fill(&ssd, !cor);
    ssd1306_rect(&ssd, 31, 63, 8, 8, cor, !cor); // Desenha um retângulo    
    ssd1306_send_data(&ssd);
}


/**
 * @brief Configura LEDs RGB como saídas PWM.
*/
void setup_pwm_leds() {
    gpio_set_function(LED_B, GPIO_FUNC_PWM);
    gpio_set_function(LED_R, GPIO_FUNC_PWM);

    // Obtém os números dos canais PWM para os pinos
    slice_blue = pwm_gpio_to_slice_num(LED_B);
    slice_red = pwm_gpio_to_slice_num(LED_R);

    // Configuração da frequência PWM
    pwm_set_clkdiv(slice_red, (float)clock_get_hz(clk_sys) / PWM_FREQ / (PWM_WRAP + 1));
    pwm_set_clkdiv(slice_blue, (float)clock_get_hz(clk_sys) / PWM_FREQ / (PWM_WRAP + 1));

    // Configura o wrap do contador PWM para 8 bits (256)
    pwm_set_wrap(slice_blue, PWM_WRAP); 
    pwm_set_wrap(slice_red, PWM_WRAP);
    
    // Habilita o PWM
    pwm_set_enabled(slice_red, 1);
    pwm_set_enabled(slice_blue, 1);
    leds_pwm_state = 1;
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
