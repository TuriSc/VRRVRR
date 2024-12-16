/**
 * @file config.h
 * @brief Configuration constants for the metronome project.
 */

#ifndef CONFIG_H_
#define CONFIG_H_

/**
 * @defgroup DeviceIdentifiers Device Identifiers
 * @{
 */
#define PROGRAM_NAME           "VRRVRR"
#define PROGRAM_VERSION        "1.0.1"
#define PROGRAM_DESCRIPTION    "LED-flashing, haptic metronome with presets and tap tempo. Written for Raspberry Pi Pico."
#define PROGRAM_URL            "https://turiscandurra.com/circuits"
/** @} */

/**
 * @defgroup RGBLED RGB LED Pin Definitions
 * @{
 */
#define RGB_R_PIN               20
#define RGB_G_PIN               21
#define RGB_B_PIN               19
#define RGB_R_PIN_DESCRIPTION       "RGB LED red"
#define RGB_G_PIN_DESCRIPTION       "RGB LED green"
#define RGB_B_PIN_DESCRIPTION       "RGB LED blue"
/** @} */

/**
 * @defgroup Motor Motor Pin Definitions
 * @{
 */
#define MOTOR_PIN               11
#define MOTOR_PIN_DESCRIPTION   "PWM vibration"
/** @} */

/**
 * @defgroup VibrationSwitch Vibration Switch Pin Definitions
 * @{
 */
#define VIBR_SWITCH_PIN         10
#define VIBR_PIN_DESCRIPTION    "Vibration switch"
/** @} */

/**
 * @defgroup LowBatteryLED Low Battery LED Pin Definitions
 * @{
 */
#define LOW_BATT_LED_PIN        8
#define LOW_BATT_LED_DESCRIPTION    "Low battery LED"
/** @} */

/**
 * @defgroup InputTimeout Input Timeout Constants
 * @{
 */
#define INPUT_TIMEOUT_MS        2000    // After this interval, any unsubmitted input will be discarded
/** @} */

/**
 * @defgroup BlinkDuration Blink Duration Constants
 * @{
 */
#define BLINK_DURATION_MS       100
#define VIBRATION_DURATION_MS   100
#define NOTIF_DURATION_MS       500
/** @} */

/**
 * @defgroup InactiveTimeout Inactive Timeout Constants
 * @{
 */
#define INACTIVE_TIMEOUT        10*60*1000*1000 // Ten minutes, in us
/** @} */

/**
 * @defgroup DefaultPresets Default Presets
 * @{
 */
#define DEFAULT_TEMPO_PRESETS   {60, 90, 60, 150}
#define DEFAULT_SUBDIV_PRESETS  {1, 1, 2, 1}  // beat subdivisions. 1 (no subdivisions) to 9
#define DEFAULT_ACCENT_PRESETS  {0, 0, 1, 0}  // 0 = disable accents, 1 = enable accents
/** @} */

/**
 * @defgroup Keypad Keypad Pin Definitions
 * @{
 */
#define KEYPAD_COLS             {4, 5, 6, 7}  // Keypad matrix column GPIOs
#define KEYPAD_ROWS             {0, 1, 2, 3}  // Keypad matrix row GPIOs
/** @} */

/**
 * @defgroup Flash Flash Constants
 * @{
 */
// Reserve the last 4KB of the default 2MB flash for persistence.
#define FLASH_TARGET_OFFSET (FLASH_SECTOR_SIZE*511)
#define MAGIC_NUMBER {0x42, 0x50, 0x4D} // 'BPM' - used as a magic number
#define MAGIC_NUMBER_LENGTH 3
/** @} */

/**
 * @defgroup Digit Digit Constants
 * @{
 */
#define DIGIT       0
#define LETTER      1
#define ASTERISK    2
#define HASH        3
/** @} */

/**
 * @defgroup Color Color Constants
 * @{
 */
#define WHITE       0
#define PURPLE      1
#define RED         2
#define GREEN       3
/** @} */

#endif /* CONFIG_H_ */

