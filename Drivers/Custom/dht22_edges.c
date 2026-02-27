#include "dht22_edges.h"
#include "dwt_delay.h"
#include <string.h>

// ===== USER CONFIG =====
extern TIM_HandleTypeDef htim2;

#define DHT_PORT GPIOA
#define DHT_PIN  GPIO_PIN_1               // PA1
#define DHT_AF   GPIO_AF1_TIM2            // PA1 AF1 TIM2
#define DHT_CH   TIM_CHANNEL_2

// How many edges to store (82-ish typical). Use margin.
#define MAX_EDGES 90

// How long without edges before we assume frame ended (us)
#define SILENCE_US 500
// Overall timeout (ms)
#define TRANS_TIMEOUT_MS 50
// =======================

typedef enum { S_IDLE=0, S_CAPTURING=1, S_READY=2 } state_t;
static volatile state_t st = S_IDLE;

static volatile uint16_t edge_t[MAX_EDGES];
static volatile uint8_t  edge_level[MAX_EDGES];  // pin level AFTER the edge (1=rising, 0=falling)
static volatile uint8_t  edgeCount = 0;

static volatile uint16_t lastEdgeCounter = 0;
static volatile uint32_t start_ms = 0;
static volatile uint8_t  haveNewEdge = 0;

// ---------- GPIO helpers ----------
static void DHT_SetOutputOD(void)
{
    GPIO_InitTypeDef g = {0};
    g.Pin = DHT_PIN;
    g.Mode = GPIO_MODE_OUTPUT_OD;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DHT_PORT, &g);
}

static void DHT_SetAF_Input(void)
{
    GPIO_InitTypeDef g = {0};
    g.Pin = DHT_PIN;
    g.Mode = GPIO_MODE_AF_PP;    // input capture uses AF; push-pull is OK for input
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = DHT_AF;
    HAL_GPIO_Init(DHT_PORT, &g);
}
// ----------------------------------

void DHT22_Edges_Init(void)
{
    // Ensure DWT delay is available
    DWT_Delay_Init();
}

// Start one transaction (non-blocking)
void DHT22_Edges_Start(void)
{
    if (st != S_IDLE) return;

    // reset capture buffers
    edgeCount = 0;
    haveNewEdge = 0;
    memset((void*)edge_t, 0, sizeof(edge_t));
    memset((void*)edge_level, 0, sizeof(edge_level));

    // Stop timer capture if it was left running
    HAL_TIM_IC_Stop_IT(&htim2, DHT_CH);
    __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_CC2);
    __HAL_TIM_SET_COUNTER(&htim2, 0);

    // Host start: pull low >=1ms (2ms safe)
    DHT_SetOutputOD();
    HAL_GPIO_WritePin(DHT_PORT, DHT_PIN, GPIO_PIN_RESET);
    HAL_Delay(2);

    // Release bus by switching to AF (do not drive high)
    DHT_SetAF_Input();
    delay_us(30);   // DHT22 expects ~20–40us after release [2](https://mikrocontroller.ti.bfh.ch/halDoc/group__TIM__Input__Capture__Polarity.html)

    // Configure capture on BOTH edges so we store all transitions
    __HAL_TIM_SET_COUNTER(&htim2, 0);

    // Force BOTHEDGE in hardware (CubeMX can also set it)
    __HAL_TIM_SET_CAPTUREPOLARITY(&htim2, DHT_CH, TIM_INPUTCHANNELPOLARITY_BOTHEDGE);

    st = S_CAPTURING;
    start_ms = HAL_GetTick();

    HAL_TIM_IC_Start_IT(&htim2, DHT_CH);
}

// Call this frequently (e.g. inside your BUSY polling loop)
void DHT22_Edges_Service(void)
{
    if (st != S_CAPTURING) return;

    // Overall timeout
    if ((HAL_GetTick() - start_ms) > TRANS_TIMEOUT_MS) {
        HAL_TIM_IC_Stop_IT(&htim2, DHT_CH);
        st = S_IDLE;
        return;
    }

    // Stop when we have enough edges
    if (edgeCount >= MAX_EDGES) {
        HAL_TIM_IC_Stop_IT(&htim2, DHT_CH);
        st = S_READY;
        return;
    }

    // If no new edges for SILENCE_US, assume frame ended
    if (haveNewEdge) {
        uint16_t now = (uint16_t)__HAL_TIM_GET_COUNTER(&htim2);
        uint16_t silence = (uint16_t)(now - lastEdgeCounter);
        if (silence > SILENCE_US) {
            HAL_TIM_IC_Stop_IT(&htim2, DHT_CH);
            st = S_READY;
        }
    }
}

// Timer callback: store edges
void DHT22_Edges_TIM_IC_Callback(TIM_HandleTypeDef *htim)
{
    if (st != S_CAPTURING) return;
    if (htim->Instance != TIM2) return;
    if (htim->Channel  != HAL_TIM_ACTIVE_CHANNEL_2) return;

    uint16_t cap = HAL_TIM_ReadCapturedValue(htim, DHT_CH);

    if (edgeCount < MAX_EDGES) {
        edge_t[edgeCount] = cap;

        // Read pin level AFTER the edge. If high -> rising edge just occurred; if low -> falling.
        edge_level[edgeCount] = (HAL_GPIO_ReadPin(DHT_PORT, DHT_PIN) == GPIO_PIN_SET) ? 1 : 0;

        edgeCount++;
        lastEdgeCounter = cap;
        haveNewEdge = 1;
    }

    // If buffer is full, stop
    if (edgeCount >= MAX_EDGES) {
        HAL_TIM_IC_Stop_IT(&htim2, DHT_CH);
        st = S_READY;
    }
}

static DHT22_Status decode_frame(float *tC, float *rh)
{
    // Build list of HIGH pulse widths from rising->falling pairs
    uint16_t hi[50];
    int hiCount = 0;
    int i = 0;

    while (i < edgeCount - 1 && hiCount < 50) {
        // Rising edge: pin level becomes 1
        if (edge_level[i] == 1) {
            uint16_t t_r = edge_t[i];

            // Find next falling edge (level becomes 0)
            int j = i + 1;
            while (j < edgeCount && edge_level[j] != 0) j++;
            if (j >= edgeCount) break;

            uint16_t t_f = edge_t[j];
            uint16_t high_us = (uint16_t)(t_f - t_r);

            // Store it
            hi[hiCount++] = high_us;

            i = j + 1;
        } else {
            i++;
        }
    }

    // We expect:
    // hi[0]  ~= ACK high (~80us)
    // hi[1..40] = 40 data-bit HIGH pulses (~26us or ~70us) [2](https://mikrocontroller.ti.bfh.ch/halDoc/group__TIM__Input__Capture__Polarity.html)
    if (hiCount < 41) return DHT22_ERROR_FRAME;

    uint8_t d[5] = {0};

    // Decode 40 bits from hi[1]..hi[40]
    for (int b = 0; b < 40; b++) {
        uint16_t high_us = hi[b + 1];

        // DHT22 bit: 0 ~26-28us, 1 ~70us. Midpoint threshold ~50us is robust. [2](https://mikrocontroller.ti.bfh.ch/halDoc/group__TIM__Input__Capture__Polarity.html)
        uint8_t bit = (high_us > 50) ? 1 : 0;

        uint8_t byteIndex = b / 8;
        uint8_t bitPos    = 7 - (b % 8);     // MSB first [1](https://playwithcircuit.com/spi-communication-protocol-tutorial/)[2](https://mikrocontroller.ti.bfh.ch/halDoc/group__TIM__Input__Capture__Polarity.html)

        if (bit) d[byteIndex] |= (1U << bitPos);
    }

    // Checksum: sum of first 4 bytes, low 8 bits [1](https://playwithcircuit.com/spi-communication-protocol-tutorial/)[2](https://mikrocontroller.ti.bfh.ch/halDoc/group__TIM__Input__Capture__Polarity.html)
    uint8_t sum = (uint8_t)(d[0] + d[1] + d[2] + d[3]);
    if (sum != d[4]) return DHT22_ERROR_CHECKSUM;

    // Humidity and temperature are 16-bit values divided by 10 [1](https://playwithcircuit.com/spi-communication-protocol-tutorial/)[2](https://mikrocontroller.ti.bfh.ch/halDoc/group__TIM__Input__Capture__Polarity.html)
    uint16_t rawHum  = (uint16_t)((d[0] << 8) | d[1]);
    uint16_t rawTemp = (uint16_t)((d[2] << 8) | d[3]);

    int16_t signedTemp = (rawTemp & 0x8000) ? -(int16_t)(rawTemp & 0x7FFF) : (int16_t)rawTemp;

    *rh = rawHum * 0.1f;
    *tC = signedTemp * 0.1f;

    return DHT22_OK;
}

DHT22_Status DHT22_Edges_Read(float *tC, float *rh)
{
    if (st == S_CAPTURING) return DHT22_BUSY;
    if (st == S_IDLE)      return DHT22_ERROR_TIMEOUT;   // treat as failed/aborted
    // st == S_READY
    DHT22_Status r = decode_frame(tC, rh);
    st = S_IDLE;  // consume frame
    return r;
}