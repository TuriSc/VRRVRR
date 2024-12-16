/**
 * @file main.c
 * @brief VRRVRR - LED-flashing, haptic metronome with presets and tap tempo.
 * Written for Raspberry Pi Pico.
 * @author Turi Scandurra
 */

#include <stdio.h>
#include <pico/stdlib.h>
#include "pico/binary_info.h"
#include "hardware/pwm.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/xosc.h"
#include "hardware/adc.h"
#include "config.h"
#include "keypad.h"             // https://github.com/TuriSc/RP2040-Keypad-Matrix
#include "battery-check.h"      // https://github.com/TuriSc/RP2040-Battery-Check

/**
 * @defgroup GlobalVariables Global Variables
 * @{
 */
uint8_t tempo;                  // BPM. Valid range is 1 to 255.
uint8_t subdiv = 1;             // Subdivisions of the current measure. Max 10.
bool accent = true;             // Whether to vibrate at a different frequency on the first subdivision of a beat
uint16_t tempo_prompt;
uint8_t num_taps;
uint8_t ticks;
bool paused = true;
bool recalc_interval;
uint64_t last_press;            // Used to determine when to enter energy-saving mode

uint8_t motor_pin_slice;

static alarm_id_t power_on_alarm;
static alarm_id_t blink_alarm;
static alarm_id_t vibrate_alarm;
static alarm_id_t type_timeout_alarm;
static alarm_id_t tap_timeout_alarm;
static repeating_timer_t metronome;
static repeating_timer_t tempo_change;
static repeating_timer_t inactive_alarm;

KeypadMatrix keypad;
const uint8_t cols[] = KEYPAD_COLS;
const uint8_t rows[] = KEYPAD_ROWS;
bool long_pressed_release_lock; // Used to prevent triggering a release event after a long press

uint8_t preset_buffer[FLASH_PAGE_SIZE];
uint8_t tempo_presets[4] = DEFAULT_TEMPO_PRESETS;
uint8_t subdiv_presets[4] = DEFAULT_SUBDIV_PRESETS;
uint8_t accent_presets[4] = DEFAULT_ACCENT_PRESETS;
/** @} */

bool tick();
int64_t blink_complete();
int64_t vibrate_complete();

/**
 * @defgroup FlashFunctions Flash Functions
 * @{
 */
/**
 * @brief Write the tempo presets to flash memory.
 */
void write_flash_presets() {
    uint8_t flash_buffer[FLASH_PAGE_SIZE] = MAGIC_NUMBER; // Initialize the buffer with a signature
    for(uint8_t i=0; i<4; i++){
        flash_buffer[MAGIC_NUMBER_LENGTH + i] = tempo_presets[i];
        flash_buffer[MAGIC_NUMBER_LENGTH + i + 4] = subdiv_presets[i];
        flash_buffer[MAGIC_NUMBER_LENGTH + i + 8] = accent_presets[i];
    }
    uint32_t ints_id = save_and_disable_interrupts();
	flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE); // Required for flash_range_program to work
	flash_range_program(FLASH_TARGET_OFFSET, flash_buffer, FLASH_PAGE_SIZE);
	restore_interrupts (ints_id);
}

/**
 * @brief Read the tempo presets from flash memory.
 */
void read_flash_presets(){ // Only called at startup
    // Read address is different than write address
    const uint8_t *stored_presets = (const uint8_t *) (XIP_BASE + FLASH_TARGET_OFFSET);

    // Validation
    uint8_t magic[3] = MAGIC_NUMBER;
    bool invalid_data = false;
    for(uint8_t i=0; i<MAGIC_NUMBER_LENGTH; i++){
        if(stored_presets[i] != magic[i]){ invalid_data = true; }
    }
    for(uint8_t i=0; i<4; i++){
        // Validate tempi
        if(stored_presets[MAGIC_NUMBER_LENGTH + i] < 1
        || stored_presets[MAGIC_NUMBER_LENGTH + i] > 255 ){ invalid_data = true; }
        // Validate subdivisions
        if(stored_presets[MAGIC_NUMBER_LENGTH + i + 4] < 1
        || stored_presets[MAGIC_NUMBER_LENGTH + i + 4] > 10 ){ invalid_data = true; }
        // Validate accents
        if(stored_presets[MAGIC_NUMBER_LENGTH + i + 8] > 1 ){ invalid_data = true; }
    }
    if(!invalid_data){
        // Presets are valid and can be loaded safely
        for(uint8_t i=0; i<4; i++){
            tempo_presets[i] = stored_presets[MAGIC_NUMBER_LENGTH + i];
            subdiv_presets[i] = stored_presets[MAGIC_NUMBER_LENGTH + i + 4];
            accent_presets[i] = stored_presets[MAGIC_NUMBER_LENGTH + i + 8];
        }
    }
}
/** @} */

/**
 * @defgroup SupportingFunctions Supporting Functions
 * @{
 */

bool inactive_check(){
    if(paused && (time_us_64() - last_press > INACTIVE_TIMEOUT)){
        // Enter dormant mode to save energy
        xosc_dormant();
    }
    return true;
}

/**
 * @brief Battery low callback.
 * @param battery_mv Battery voltage in millivolts.
 */
void battery_low_callback(uint16_t battery_mv){
    gpio_put(LOW_BATT_LED_PIN, 1);
    battery_check_stop();
}

/**
 * @brief Declare all program information.
 * 
 */
void bi_decl_all(){
    bi_decl(bi_program_name(PROGRAM_NAME));
    bi_decl(bi_program_description(PROGRAM_DESCRIPTION));
    bi_decl(bi_program_version_string(PROGRAM_VERSION));
    bi_decl(bi_program_url(PROGRAM_URL));
    bi_decl(bi_3pins_with_names(RGB_R_PIN, RGB_R_PIN_DESCRIPTION,
    RGB_G_PIN, RGB_G_PIN_DESCRIPTION,
    RGB_B_PIN, RGB_B_PIN_DESCRIPTION));
    bi_decl(bi_1pin_with_name(MOTOR_PIN, MOTOR_PIN_DESCRIPTION));
    bi_decl(bi_1pin_with_name(VIBR_SWITCH_PIN, VIBR_PIN_DESCRIPTION));
    bi_decl(bi_1pin_with_name(LOW_BATT_LED_PIN, LOW_BATT_LED_DESCRIPTION));
}

/**
 * @brief Convert beats per minute to interval in microseconds.
 * 
 * @param t Beats per minute.
 * @return Interval in microseconds.
 */
uint64_t bpm_to_interval(uint8_t t){
    return (uint64_t)((60 * 1000 * 1000) / t);
}

/**
 * @brief Convert interval in microseconds to beats per minute.
 * 
 * @param interval Interval in microseconds.
 * @return Beats per minute.
 */
uint8_t interval_to_bpm(uint64_t interval){
    return (uint8_t)((60*1000*1000) / interval);
}
/** @} */

/**
 * @defgroup LEDFunctions LED Functions
 * @{
 */
/**
 * @brief Set the RGB LED to the specified color.
 * @param r Red component of the color.
 * @param g Green component of the color.
 * @param b Blue component of the color.
 */
void rgb(bool r, bool g, bool b){
    // Since we're using common anode RGB LEDs,
    // RGB values have to be inverted 
    gpio_put(RGB_R_PIN, !r);
    gpio_put(RGB_G_PIN, !g);
    gpio_put(RGB_B_PIN, !b);
}


/**
 * @brief Blink the RGB LED for the specified duration.
 * @param ms Duration of the blink in milliseconds.
 * @param color Color of the blink.
 */
void blink(uint16_t ms, uint8_t color){ // LEDs blink for the specified time in milliseconds
    switch(color){
        case RED:
            rgb(1, 0, 0);
        break;
        case PURPLE:
            rgb(1, 0, 1);
        break;
        case WHITE:
            rgb(1, 1, 1);
        break;
        case GREEN:
            rgb(0, 1, 0);
        break;
    }
    if (blink_alarm) cancel_alarm(blink_alarm);
    blink_alarm = add_alarm_in_ms(ms, blink_complete, NULL, true);
}

/**
 * @brief Vibrate the motor for the specified duration.
 * @param ms Duration of the vibration in milliseconds.
 * @param is_first Whether this is the first subdivision of the beat.
 */
void vibrate(uint16_t ms, bool is_first){
    if(is_first){
        pwm_set_wrap(motor_pin_slice, 1);
        pwm_set_gpio_level(MOTOR_PIN, 3);
    } else {
        pwm_set_wrap(motor_pin_slice, 2);
        pwm_set_gpio_level(MOTOR_PIN, 1);
    }
    pwm_set_enabled(motor_pin_slice, true);
    if (vibrate_alarm) cancel_alarm(vibrate_alarm);
    vibrate_alarm = add_alarm_in_ms(ms, vibrate_complete, NULL, true);
}
/** @} */

/**
 * @defgroup AlarmFunctions Alarm Functions
 * @{
 */
/**
 * @brief Alarm handler for the power-on alarm.
 * @return 0 on success.
 */
int64_t power_on_complete(){
    gpio_put(PICO_DEFAULT_LED_PIN, 0);
    rgb(0, 0, 0); // Off
    return 0;
}

/**
 * @brief Alarm handler for the blink alarm.
 * @return 0 on success.
 */
int64_t blink_complete() {
    rgb(0, 0, 0); // Off
    return 0;
}

/**
 * @brief Alarm handler for the vibrate alarm.
 * @return 0 on success.
 */
int64_t vibrate_complete() {
    pwm_set_gpio_level(MOTOR_PIN, 0);
    return 0;
}

/**
 * @brief Alarm handler for the input timeout alarm.
 * @return 0 on success.
 */
int64_t input_timeout(){
    tempo_prompt = 0;
    return 0;
}

/**
 * @brief Alarm handler for the tap timeout alarm.
 * @return 0 on success.
 */
int64_t tap_timeout(){
    num_taps = 0;
    return 0;
}
/** @} */

/**
 * @defgroup MetronomeFunctions Metronome Functions
 * @{
 */
/**
 * @brief Stop the metronome.
 */
void stop(){
    cancel_repeating_timer(&metronome);
    paused = true;
}

/**
 * @brief Set the tempo of the metronome.
 * @param t Tempo in beats per minute.
 */
void set_tempo(uint8_t t){
    if(t < 1) { return; }
    tempo = t;
    ticks = 0;
    stop();
    uint64_t interval = bpm_to_interval(t);
    // Apply subdivisions
    interval /= subdiv;
    // Use a negative value for more precise ticking
    interval *= -1;
    add_repeating_timer_us(interval, tick, NULL, &metronome);
    paused = false;
}

/**
 * @brief Tick function for the metronome.
 * @return true on success
 */
bool tick() {
    bool is_first = false;
    if(accent && ticks == 0){
        // The first subdivision, the actual beat
        is_first = true;
        blink(BLINK_DURATION_MS, PURPLE);
    } else {
        blink(BLINK_DURATION_MS, WHITE);
    }

    if(!gpio_get(VIBR_SWITCH_PIN)) { vibrate(VIBRATION_DURATION_MS, is_first); }

    if(++ticks >= subdiv) { ticks = 0; }

    if(recalc_interval){ // Tempo is being increased or decreased using + or - keys
        stop();
        if(tempo > 0) { set_tempo(tempo); } // Restart
        recalc_interval = false;
    }
    return true;
}

/**
 * @brief Increase the tempo of the metronome.
 * @return true on success
 */
bool increase_tempo(){
    if(tempo > 0) { tempo--; }
    recalc_interval = true;
    return true;
}

/**
 * @brief Decrease the tempo of the metronome.
 * @return true on success
 */
bool decrease_tempo(){
    if(tempo < 256) { tempo++; }
    recalc_interval = true;
    return true;
}

/**
 * @brief Increase the tempo of the metronome while holding the + key.
 */
void increase_tempo_hold(){
    cancel_repeating_timer(&tempo_change);
    add_repeating_timer_ms(50, increase_tempo, NULL, &tempo_change);
    long_pressed_release_lock = false;
}

/**
 * @brief Decrease the tempo of the metronome while holding the - key.
 */
void decrease_tempo_hold(){
    cancel_repeating_timer(&tempo_change);
    add_repeating_timer_ms(50, decrease_tempo, NULL, &tempo_change);
    long_pressed_release_lock = false;
}

/**
 * @brief Set the measure of the metronome.
 * @param m Measure of the metronome.
 */
void set_measure(uint8_t m){
    if(m < 1 || m > 9) { return; }
    subdiv = m;
    stop();
    if(tempo > 0) { set_tempo(tempo); } // Restart
}

/**
 * @brief Toggle the pause state of the metronome.
 */
// Implemented but not currently used
void toggle_pause(){
    if(paused = !paused){
        stop();
    } else {
        if(tempo > 0) { set_tempo(tempo); }
    }
}

/**
 * @brief Toggle the accent state of the metronome.
 */
void toggle_accent(){
    accent = !accent;
}

/**
 * @brief Type a tempo value.
 * @param n Digit to type.
 */
void type_tempo(uint8_t n){
    stop();
    if(type_timeout_alarm) { cancel_alarm (type_timeout_alarm); }
    type_timeout_alarm = add_alarm_in_ms(INPUT_TIMEOUT_MS, input_timeout, NULL, true);
    tempo_prompt *= 10;
    tempo_prompt += n;
    if(tempo_prompt > 0 && tempo_prompt < 256){
        set_tempo((uint8_t)tempo_prompt);
    }
}

/**
 * @brief Tap the tempo.
 */
void tap(){
    stop();
    if(tap_timeout_alarm) { cancel_alarm (tap_timeout_alarm); }
    tap_timeout_alarm = add_alarm_in_ms(INPUT_TIMEOUT_MS, tap_timeout, NULL, true);
    static uint64_t tap_interval_avg;
    static uint64_t last_tap;
    uint64_t now = time_us_64();

    if(++num_taps > 1) {
        tap_interval_avg = (tap_interval_avg + (now - last_tap)) / 2; // Average past and current tap tempi
        set_tempo(interval_to_bpm(tap_interval_avg));
    }
    last_tap = now;
}

/**
 * @brief Save a preset.
 * @param c Preset number.
 */
void save_preset(uint8_t c){
    if(tempo == 0) { return; }
    tempo_presets[c] = tempo;
    subdiv_presets[c] = subdiv;
    accent_presets[c] = accent;
    stop();
    blink(NOTIF_DURATION_MS, GREEN);
    write_flash_presets();
    sleep_ms(NOTIF_DURATION_MS); // Prevent other events from accessing the LEDs
    set_tempo(tempo); // Restart
}

/**
 * @brief Apply a preset.
 * @param c Preset number.
 */
void apply_preset(uint8_t c){
    tempo = tempo_presets[c];
    accent = accent_presets[c];
    set_measure(subdiv_presets[c]);
}

/**
 * @brief Key press handler.
 * @param key Key that was pressed.
 */
void key_pressed(uint8_t key){
    last_press = time_us_64();  // Used for dormant mode

    switch(key){
        case 12: // Asterisk
            decrease_tempo();
            break;
        case 14: // Little gate symbol
            increase_tempo();
            break;
    }
}

/**
 * @brief Key release handler.
 * @param key Key that was released.
 */
void key_released(uint8_t key){
    if(long_pressed_release_lock) {
        long_pressed_release_lock = false;
        return;
    }

    switch(key){
        case 0:
            type_tempo(1);
            break;
        case 1:
            type_tempo(2);
            break;
        case 2:
            type_tempo(3);
            break;
        case 4:
            type_tempo(4);
            break;
        case 5:
            type_tempo(5);
            break;
        case 6:
            type_tempo(6);
            break;
        case 8:
            type_tempo(7);
            break;
        case 9:
            type_tempo(8);
            break;
        case 10:
            type_tempo(9);
            break;
        case 13:
            if(tempo_prompt > 0) {  // User is already typing a number
                type_tempo(0);      // Treat it as a '0' digit
            } else {                // User is not typing a number
                tap();              // Use the button to tap tempo
            }
            break;

        case 3:  // A
            apply_preset(0);
            break;
        case 7:  // B
            apply_preset(1);
            break;
        case 11: // C
            apply_preset(2);
            break;
        case 15: // D
            apply_preset(3);
            break;

        case 12:
        case 14:
            cancel_repeating_timer(&tempo_change);
            break;
    }

    blink(BLINK_DURATION_MS, RED); // Feedback blink
}

/**
 * @brief Key long press handler.
 * @param key Key that was long pressed.
 */
void key_long_pressed(uint8_t key){
    long_pressed_release_lock = true;
    switch(key){
        case 0:
            set_measure(1);
            break;
        case 1:
            set_measure(2);
            break;
        case 2:
            set_measure(3);
            break;
        case 4:
            set_measure(4);
            break;
        case 5:
            set_measure(5);
            break;
        case 6:
            set_measure(6);
            break;
        case 8:
            set_measure(7);
            break;
        case 9:
            set_measure(8);
            break;
        case 10:
            set_measure(9);
            break;
        case 13:
            toggle_accent();
            break;

        case 3:  // A
            save_preset(0);
            break;
        case 7:  // B
            save_preset(1);
            break;
        case 11: // C
            save_preset(2);
            break;
        case 15: // D
            save_preset(3);
            break;

        case 12: // Asterisk
            decrease_tempo_hold();
            break;
        case 14: // Little gate symbol
            increase_tempo_hold();
            break;
    }
}
/** @} */

/**
 * @brief Main entry point.
 * @return 0 on success.
 */
int main() {
    stdio_init_all();
    bi_decl_all();

    gpio_init(RGB_R_PIN);
    gpio_set_dir(RGB_R_PIN, GPIO_OUT);
    gpio_init(RGB_G_PIN);
    gpio_set_dir(RGB_G_PIN, GPIO_OUT);
    gpio_init(RGB_B_PIN);
    gpio_set_dir(RGB_B_PIN, GPIO_OUT);

    gpio_init(VIBR_SWITCH_PIN);
    gpio_set_dir(VIBR_SWITCH_PIN, GPIO_IN);
    gpio_pull_up(VIBR_SWITCH_PIN);

    gpio_init(MOTOR_PIN);
    gpio_set_function(MOTOR_PIN, GPIO_FUNC_PWM);
    motor_pin_slice = pwm_gpio_to_slice_num(MOTOR_PIN);
    
    // Use the onboard LED as a power-on indicator
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, 1);
    power_on_alarm = add_alarm_in_ms(500, power_on_complete, NULL, true);

    gpio_init(LOW_BATT_LED_PIN);
    gpio_set_dir(LOW_BATT_LED_PIN, GPIO_OUT);

    adc_init();
    battery_check_init(5000, NULL, battery_low_callback);

    add_repeating_timer_ms(5000, inactive_check, NULL, &inactive_alarm);

    // Initialize the keypad with column and row configuration
    // And declare the number of columns and rows of the keypad
    keypad_init(&keypad, cols, rows, 4, 4);

     // Assign the callbacks for each keypad event
    keypad_on_press(&keypad, key_pressed);
    keypad_on_long_press(&keypad, key_long_pressed);
    keypad_on_release(&keypad, key_released);

    // Attempt to load the tempo presets, if they were previously stored on flash
    read_flash_presets();

    while (true) {
        keypad_read(&keypad);
        sleep_ms(5);
    }

    return 0;
}


