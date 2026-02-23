/*
 * my_accessory.c
 * Define the accessory in C language using the Macro in characteristics.h
 *
 *  Created on: 2020-05-15
 *      Author: Mixiaoxiao (Wang Bin)
 */

#include <homekit/homekit.h>
#include <homekit/characteristics.h>

void my_accessory_identify(homekit_value_t _value) {
	printf("accessory identify\n");
}

homekit_characteristic_t cha_on = HOMEKIT_CHARACTERISTIC_(ON, false);
homekit_characteristic_t cha_name = HOMEKIT_CHARACTERISTIC_(NAME, "Mazy's Magic Leds");
homekit_characteristic_t cha_effect_on = HOMEKIT_CHARACTERISTIC_(ON, false);
homekit_characteristic_t cha_bright = HOMEKIT_CHARACTERISTIC_(BRIGHTNESS, 100);

homekit_accessory_t *accessories[] = {
    HOMEKIT_ACCESSORY(.id=1, .category=homekit_accessory_category_lightbulb, .services=(homekit_service_t*[]) {
        HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics=(homekit_characteristic_t*[]) {
            &cha_name,
            HOMEKIT_CHARACTERISTIC(MANUFACTURER, "Mazy Wunderwaffel Technology"),
            HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER, "1111-1111"),
            HOMEKIT_CHARACTERISTIC(MODEL, "ESP/WS82xx"),
            HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, "3.01"),
            HOMEKIT_CHARACTERISTIC(IDENTIFY, my_accessory_identify),
            NULL
        }),
        HOMEKIT_SERVICE(LIGHTBULB, .primary=true, .characteristics=(homekit_characteristic_t*[]) {
            HOMEKIT_CHARACTERISTIC(NAME, "Leds"),
            &cha_on,
            &cha_bright,
            NULL
        }),


        HOMEKIT_SERVICE(SWITCH, .characteristics=(homekit_characteristic_t*[]){
            HOMEKIT_CHARACTERISTIC(NAME, "Next Effect"),
            &cha_effect_on,
            NULL
        }),



        NULL
    }),
    NULL
};

homekit_server_config_t accessory_config = {
    .accessories = accessories,
    .password = "111-11-111"
};
