#pragma once

#include <stdio.h>

typedef unsigned int ui;

#define FALSE 0
#define TRUE !FALSE

#define MAX_SIZE 256
#define BIT(n) (0x01 << n)

int getFileSize(FILE* file);
