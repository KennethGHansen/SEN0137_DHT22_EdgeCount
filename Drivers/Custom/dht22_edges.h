#ifndef DHT22_EDGES_H
#define DHT22_EDGES_H

#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_tim.h"

typedef enum {
    DHT22_OK = 0,
    DHT22_BUSY,
    DHT22_ERROR_TIMEOUT,
    DHT22_ERROR_CHECKSUM,
    DHT22_ERROR_FRAME
} DHT22_Status;

void DHT22_Edges_Init(void);
void DHT22_Edges_Start(void);
void DHT22_Edges_Service(void);                    // call frequently while BUSY
DHT22_Status DHT22_Edges_Read(float *tC, float *rh); // call after Start + Service loop
void DHT22_Edges_TIM_IC_Callback(TIM_HandleTypeDef *htim);

#endif