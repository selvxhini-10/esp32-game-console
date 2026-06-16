#pragma once

typedef struct {
    int x;
    int y;
    int button;
} joystick_data_t;

void joystick_init(void);
joystick_data_t joystick_read(void);