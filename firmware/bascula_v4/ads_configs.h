#pragma once
#include <stdint.h>

#define ADS_REG_CONVERSION  0x00
#define ADS_REG_CONFIG      0x01
#define ADS_ADDR_REAR   0x48
#define ADS_ADDR_FRONT  0x49
#define ADS_CONFIG_WORD_REAR   0x0AE3
#define ADS_CONFIG_WORD_FRONT  0x0AE3
static const float ADS_MV_PER_LSB = 0.03125f;
