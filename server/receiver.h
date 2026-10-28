#ifndef RECEIVER_H
#define RECEIVER_H

#include "stdio.h"
#include "stdint.h"
#include "string.h"
#include "time.h"
#include "rsvisa/visa.h"
#include <stdbool.h>
#include <stdlib.h>

int init_receiver(int start_freq, int stop_freq);

int measure();

int get_data(int measure_freq, double *value);

#endif