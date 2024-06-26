/*
 * LED blink with FreeRTOS
 */
#include "hardware/adc.h"
#include <FreeRTOS.h>
#include <math.h>
#include <queue.h>
#include <semphr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <task.h>

#include "hc06.h"
#include "pico/stdlib.h"
#include "ssd1306.h"

/* List of IDs
B      - 0
Y      - 1
X      - 2
A      - 3
TR     - 4
TL     - 5
CJS X  - 6
CJS Y  - 7
MJS X  - 8
MJS Y  - 9 */

/* ------------------------------ Constants ------------------------------ */
#define DEBOUNCE_TIME 200

#define GAME_BTN_B_PIN 10
#define GAME_BTN_Y_PIN 11
#define GAME_BTN_X_PIN 12
#define GAME_BTN_A_PIN 13
#define R_TRIGGER_PIN 14 // a mudar
#define L_TRIGGER_PIN 15 // a mudar

#define R_JOYSTICK_SW_PIN 21 // a mudar
#define L_JOYSTICK_SW_PIN 20 // a mudar

#define MUX_A_CONTROL_PIN 16
#define MUX_ADC_PIN 28

#define DEAD_ZONE 180

/* ------------------------------ Data structures ------------------------------ */
typedef struct adc {
    int axis;
    int val;
} adc_t;

typedef struct {
    int last_val_x; // last value sent for x-axis
    int last_val_y; // last value sent for y-axis
} joystick_state_t;

/* ------------------------------ Global variables ------------------------------ */
QueueHandle_t xQueueGameButton, xQueueJoyStick, xQueueBluetooth, xQueueJoyStickLeft;

/* ------------------------------ Utilities ------------------------------ */
bool has_debounced(uint32_t current_trigger, uint32_t last_trigger) {
    return current_trigger - last_trigger > DEBOUNCE_TIME;
}

void write_package(adc_t data) {
    uart_putc_raw(HC06_UART_ID, data.axis);
    uart_putc_raw(HC06_UART_ID, data.val >> 8);
    uart_putc_raw(HC06_UART_ID, data.val & 0xFF);
    uart_putc_raw(HC06_UART_ID, -1);
}

/* ------------------------------ Callbacks ------------------------------ */
void game_btn_callback(uint gpio, uint32_t events) {
    uint pressed = 0;

    if (events == 0x4) { // fall edge
        if (gpio == GAME_BTN_B_PIN)
            pressed = 0;
        else if (gpio == GAME_BTN_Y_PIN)
            pressed = 1;
        else if (gpio == GAME_BTN_X_PIN)
            pressed = 2;
        else if (gpio == GAME_BTN_A_PIN)
            pressed = 3;
        else if (gpio == R_TRIGGER_PIN)
            pressed = 4;
        else if (gpio == L_TRIGGER_PIN)
            pressed = 5;
        else if (gpio == R_JOYSTICK_SW_PIN)
            pressed = 6;
        else if (gpio == L_JOYSTICK_SW_PIN)
            pressed = 7;
    }

    xQueueSendFromISR(xQueueGameButton, &pressed, 0);
}

/* ------------------------------ Tasks ------------------------------ */
void hc06_task(void *p) {
    printf("HC06 Task\n");
    uart_init(HC06_UART_ID, HC06_BAUD_RATE);
    gpio_set_function(HC06_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(HC06_RX_PIN, GPIO_FUNC_UART);
    hc06_init("bruno-stanz", "1234");

    adc_t data;

    while (1) {
        if (xQueueReceive(xQueueBluetooth, &data, pdMS_TO_TICKS(10))) {
            write_package(data);
        }
    }
}

void game_btn_task(void *p) {
    gpio_init(GAME_BTN_B_PIN);
    gpio_set_dir(GAME_BTN_B_PIN, GPIO_IN);
    gpio_pull_up(GAME_BTN_B_PIN);

    gpio_init(GAME_BTN_Y_PIN);
    gpio_set_dir(GAME_BTN_Y_PIN, GPIO_IN);
    gpio_pull_up(GAME_BTN_Y_PIN);

    gpio_init(GAME_BTN_X_PIN);
    gpio_set_dir(GAME_BTN_X_PIN, GPIO_IN);
    gpio_pull_up(GAME_BTN_X_PIN);

    gpio_init(GAME_BTN_A_PIN);
    gpio_set_dir(GAME_BTN_A_PIN, GPIO_IN);
    gpio_pull_up(GAME_BTN_A_PIN);

    gpio_init(R_TRIGGER_PIN);
    gpio_set_dir(R_TRIGGER_PIN, GPIO_IN);
    gpio_pull_up(R_TRIGGER_PIN);

    gpio_init(L_TRIGGER_PIN);
    gpio_set_dir(L_TRIGGER_PIN, GPIO_IN);
    gpio_pull_up(L_TRIGGER_PIN);

    gpio_init(L_JOYSTICK_SW_PIN);
    gpio_set_dir(L_JOYSTICK_SW_PIN, GPIO_IN);
    gpio_pull_up(L_JOYSTICK_SW_PIN);

    gpio_init(R_JOYSTICK_SW_PIN);
    gpio_set_dir(R_JOYSTICK_SW_PIN, GPIO_IN);
    gpio_pull_up(R_JOYSTICK_SW_PIN);

    gpio_set_irq_enabled_with_callback(GAME_BTN_B_PIN,
                                       GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE,
                                       true, &game_btn_callback);
    gpio_set_irq_enabled(GAME_BTN_Y_PIN, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(GAME_BTN_X_PIN, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(GAME_BTN_A_PIN, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(R_TRIGGER_PIN, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(L_TRIGGER_PIN, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(L_JOYSTICK_SW_PIN, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(R_JOYSTICK_SW_PIN, GPIO_IRQ_EDGE_FALL, true);

    uint32_t b_btn_last_trigger = 0;
    uint32_t y_btn_last_trigger = 0;
    uint32_t x_btn_last_trigger = 0;
    uint32_t a_btn_last_trigger = 0;
    uint32_t tr_btn_last_trigger = 0;
    uint32_t tl_btn_last_trigger = 0;
    uint32_t r_joysw_last_trigger = 0;
    uint32_t l_joysw_last_trigger = 0;

    uint pressed_button = 0;
    uint32_t trigger_time;

    adc_t data;

    while (true) {
        if (xQueueReceive(xQueueGameButton, &pressed_button, pdMS_TO_TICKS(10))) {
            trigger_time = to_ms_since_boot(get_absolute_time());
            if (pressed_button == 0 && has_debounced(trigger_time, b_btn_last_trigger)) {
                b_btn_last_trigger = trigger_time;
                data.axis = 0;
                data.val = 1;
                write_package(data);
            }

            else if (pressed_button == 1 && has_debounced(trigger_time, y_btn_last_trigger)) {
                y_btn_last_trigger = trigger_time;
                data.axis = 1;
                data.val = 1;
                write_package(data);
            }

            else if (pressed_button == 2 && has_debounced(trigger_time, x_btn_last_trigger)) {
                x_btn_last_trigger = trigger_time;
                data.axis = 2;
                data.val = 1;
                write_package(data);
            }

            else if (pressed_button == 3 && has_debounced(trigger_time, a_btn_last_trigger)) {
                a_btn_last_trigger = trigger_time;
                data.axis = 3;
                data.val = 1;
                write_package(data);
            }

            else if (pressed_button == 4 && has_debounced(trigger_time, tr_btn_last_trigger)) {
                tr_btn_last_trigger = trigger_time;
                data.axis = 4;
                data.val = 1;
                write_package(data);
            }

            else if (pressed_button == 5 && has_debounced(trigger_time, tl_btn_last_trigger)) {
                tl_btn_last_trigger = trigger_time;
                data.axis = 5;
                data.val = 1;
                write_package(data);
            }

            else if (pressed_button == 6 && has_debounced(trigger_time, r_joysw_last_trigger)) {
                r_joysw_last_trigger = trigger_time;
                data.axis = 10;
                data.val = 1;
                write_package(data);
            }

            else if (pressed_button == 7 && has_debounced(trigger_time, l_joysw_last_trigger)) {
                l_joysw_last_trigger = trigger_time;
                data.axis = 11;
                data.val = 1;
                write_package(data);
            }
        }
    }
}

void x_task(void *p) {
    adc_init();
    adc_gpio_init(26);
    adc_set_round_robin(0b00011);

    adc_t data;

    while (1) {
        data.axis = 6;
        adc_select_input(0);
        data.val = adc_read();

        int mapped_val = (data.val - 2047) * 255 / 2047;
        data.val = (int)(-mapped_val);

        xQueueSend(xQueueJoyStick, &data, pdMS_TO_TICKS(10));
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void y_task(void *p) {
    adc_init();
    adc_gpio_init(27);
    adc_set_round_robin(0b00011);

    adc_t data;

    while (1) {
        data.axis = 7;
        adc_select_input(1);
        data.val = adc_read();

        int mapped_val = (data.val - 2047) * 255 / 2047;
        data.val = (int)(-mapped_val);

        xQueueSend(xQueueJoyStick, &data, pdMS_TO_TICKS(10));
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void joystick_task(void *p) {
    adc_t data;

    joystick_state_t right_joystick_state = {0, 0};

    while (1) {
        if (xQueueReceive(xQueueJoyStick, &data, pdMS_TO_TICKS(10))) {
            int *last_value;
            if (data.axis == 6) { // right joystick X-axis
                last_value = &right_joystick_state.last_val_x;
            } else if (data.axis == 7) { // right joystick Y-axis
                last_value = &right_joystick_state.last_val_y;
            } else {
                continue; // ignore invalid axis data
            }

            if (abs(data.val) > 30) {
                if (data.val != *last_value) {
                    xQueueSend(xQueueBluetooth, &data, pdMS_TO_TICKS(10));
                    *last_value = data.val;
                }
            } else {
                data.val = 0;
                if (*last_value != 0) {
                    xQueueSend(xQueueBluetooth, &data, pdMS_TO_TICKS(10));
                    *last_value = 0;
                }
            }
        }
    }
}

void mux_task(void *p) {
    adc_init();
    adc_gpio_init(MUX_ADC_PIN);
    adc_set_round_robin(0b00011);

    gpio_init(MUX_A_CONTROL_PIN);
    gpio_set_dir(MUX_A_CONTROL_PIN, GPIO_OUT);

    adc_t data;
    bool get_x = true;
    while (1) {
        if (get_x) {
            data.axis = 8;
            gpio_put(MUX_A_CONTROL_PIN, 0);
        } else {
            data.axis = 9;
            gpio_put(MUX_A_CONTROL_PIN, 1);
        }

        adc_select_input(2);
        data.val = adc_read();
        int mapped_val = (data.val - 2047) * 255 / 2047;
        data.val = (int)(-mapped_val);

        xQueueSend(xQueueJoyStickLeft, &data, pdMS_TO_TICKS(10));
        vTaskDelay(pdMS_TO_TICKS(100));

        get_x = get_x ? false : true;
    }
}

void left_joystick_task(void *p) {
    adc_t data;

    joystick_state_t left_joystick_state = {0, 0};

    while (1) {
        if (xQueueReceive(xQueueJoyStickLeft, &data, pdMS_TO_TICKS(10))) {
            int *last_value;
            if (data.axis == 8) { // left joystick X-axis
                last_value = &left_joystick_state.last_val_x;
            } else if (data.axis == 9) { // left joystick Y-axis
                last_value = &left_joystick_state.last_val_y;
            } else {
                continue; // ignore invalid axis data
            }

            if (abs(data.val) > 42) {
                if (data.val != *last_value) {
                    xQueueSend(xQueueBluetooth, &data, pdMS_TO_TICKS(10));
                    *last_value = data.val;
                }
            } else {
                data.val = 0;
                if (*last_value != 0) {
                    xQueueSend(xQueueBluetooth, &data, pdMS_TO_TICKS(10));
                    *last_value = 0;
                }
            }
        }
    }
}

void task_oled(void *p) {
    SSD1306_init();

    uint8_t buf[SSD1306_BUF_LEN];
    memset(buf, 0, SSD1306_BUF_LEN); //  clear buffer

    struct render_area area = {
        .start_col = 0,
        .end_col = SSD1306_WIDTH - 1,
        .start_page = 0,
        .end_page = SSD1306_NUM_PAGES - 1,
    };

    calc_render_area_buflen(&area); // calculate the buffer length based on the display area (128 x 32 pixels)

    char *frames[] = {
        "Now playing",
        "Now playing.",
        "Now playing..",
        "Now playing..."};

    const int num_frames = sizeof(frames) / sizeof(frames[0]);

    int current_frame = 0;
    int next_frame_cnt = 0; // this will just be a counter to change the frame every 5 iterations (500ms)

    while (1) {
        memset(buf, 0, SSD1306_BUF_LEN); // clear buffer

        WriteString(buf, 0, 0, frames[current_frame]); // write the current text/frame on the buffer

        // music visualizer
        for (int i = 0; i < SSD1306_WIDTH; i += 4) { // bar width
            int height = rand() % 12;                // random bar height
            for (int j = 0; j < height; j++) {
                SetPixel(buf, i, 31 - j, true); // draw bar from bottom to top on buffer
            }
        }

        // render the buffer to the OLED
        render(buf, &area);

        if (next_frame_cnt >= 5) {
            current_frame = (current_frame + 1) % num_frames;
            next_frame_cnt = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(150));
        next_frame_cnt += 1;
    }
}

/* ------------------------------ Main ------------------------------ */
int main() {
    stdio_init_all();
    adc_init();

    // Semaphores

    // Queues
    xQueueGameButton = xQueueCreate(32, sizeof(uint));
    if (xQueueGameButton == NULL) {
        printf("Falha em criar a fila xQueueGameButton... \n");
    }

    xQueueJoyStick = xQueueCreate(32, sizeof(adc_t));
    if (xQueueJoyStick == NULL) {
        printf("Falha em criar a fila xQueueJoyStick... \n");
    }

    xQueueBluetooth = xQueueCreate(32, sizeof(adc_t));
    if (xQueueBluetooth == NULL) {
        printf("Falha em criar a fila xQueueBluetooth... \n");
    }

    xQueueJoyStickLeft = xQueueCreate(32, sizeof(adc_t));
    if (xQueueJoyStickLeft == NULL) {
        printf("Falha em criar a fila xQueueJoyStickLeft... \n");
    }

    // Tasks
    xTaskCreate(game_btn_task, "Button Task", 4096, NULL, 3, NULL);             // maximum priority
    xTaskCreate(joystick_task, "RJ Processing Task", 4096, NULL, 3, NULL);      // maximum priority
    xTaskCreate(left_joystick_task, "LJ Processing Task", 4096, NULL, 3, NULL); // maximum priority

    xTaskCreate(hc06_task, "HC06 Task", 4096, NULL, 2, NULL);      // high priority
    xTaskCreate(x_task, "RJ X-axis Task", 4096, NULL, 2, NULL);    // high priority
    xTaskCreate(y_task, "RJ Y-axis Task", 4096, NULL, 2, NULL);    // high priority
    xTaskCreate(mux_task, "LJ XY-axis Task", 4096, NULL, 2, NULL); // high priority

    xTaskCreate(task_oled, "OLED Task", 4096, NULL, 1, NULL); // regular priority

    vTaskStartScheduler();
    while (true)
        ;
}
