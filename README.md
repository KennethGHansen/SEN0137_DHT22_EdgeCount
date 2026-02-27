# SEN0137 DHT22 Driver (Interrupt-Based Edge Capture)

Robust STM32 HAL driver for the **SEN0137 / DHT22 / AM2302** temperature and humidity sensor using **timer input capture + interrupts** and **offline edge decoding**.

This project demonstrates a reliable, timing-accurate way to read DHT22 sensors without bit-banging delays or blocking loops.

---

## Features

-  Uses **hardware timer input capture** (µs resolution)
-  Captures **all signal edges**, decodes offline
-  No reliance on fragile “last falling edge”
-  Accurate decoding of DHT22 pulse-width protocol
-  Checksum-validated temperature & humidity
-  Non-blocking, interrupt-driven design
-  Suitable for production firmware

---

## Hardware

| Component | Description |
|---------|------------|
| Sensor | SEN0137 / DHT22 / AM2302 |
| MCU | STM32F446RE (tested) |
| Timer | TIM2 |
| GPIO | PA1 (TIM2_CH2) |
| Pull-up | 4.7kΩ–10kΩ on DATA line |

- NOTE: Supply the SEN0137 with +5V and pull up with 4k7 to 3V3 for smooth functionality

---

## Timer Configuration (CubeMX)

- Timer: **TIM2**
- Channel: **CH2**
- Trigger Source: ITR0
- Clock Source: Internal clock
- Mode: Input Capture Direct
- Polarity: **Both Edges**
- Prescaler: Set for **1 µs tick** (Prescaler = 89, Period = 65535)
- Counter mode: Up
- NVIC: TIM2 interrupt enabled

---

## Sys Clock Configuration (CubeMX)
HSI -> PLLM: 8 -> PLLN: x180 -> PLLP: /2 -> 
PLLCLK -> HCLK: 180 -> APB1: /4 -> APB2: -> /2

---                                    

## Project Structure
```
SEN0137_DHT_EdgeCount/
├── Core/
│   ├── Inc/
│   └── Src/
│
├── Drivers/
│   └── Custom/
│       ├── dht22_edges.c
│       ├── dht22_edges.h
│       └── dwt_delay.h
│
├── MDK-ARM/
│   ├── SEN0137_DHT22_EdgeCount.uvprojx
│   ├── SEN0137_DHT22_EdgeCount.uvoptx
│
├── README.md
├── LICENSE
├── .gitignore
└──SEN0137_DHT22_EdgeCount.ioc
```

---

Keil users: be sure to add  
`Drivers/Custom/`  
to the **include paths in uvision**

##  Getting Started

### 1. Clone the repository

### 2. Open the Keil `.uvprojx` project file

### 3. Make sure the include paths are correct
Keil →  
**Project → Options for Target → C/C++ → Include Paths**
Add:
../Drivers/Custom

### 4. Build and flash
Press **Build** and **Download** in Keil.

---

##  How It Works (High-Level)

1. MCU pulls DATA line LOW (~2 ms) to start transaction  
2. MCU releases line and switches GPIO to timer input  
3. Timer captures **every rising and falling edge** via interrupt  
4. Edge timestamps are stored in RAM  
5. After transmission ends, decoding is done **offline**:
   - HIGH pulse width ≈ 26–28 µs → `0`
   - HIGH pulse width ≈ 70 µs → `1`
6. 40 bits are assembled MSB-first into 5 bytes  
7. Checksum is verified  
8. Temperature & humidity are returned

This approach avoids timing races and edge-loss issues common in bit-banged drivers.

---

## Flow Chart
<img width="1000" height="1400" alt="default_image_001" src="https://github.com/user-attachments/assets/e6e3a28a-eab1-4010-8ff4-9ef527c61934" />

---

## Driver flowchart detailed description
1️⃣ Start
This is the application loop deciding to read the DHT22.

2️⃣ DHT22_Edges_Start()
Purpose: Initiate a single DHT22 transaction.
What happens in code:
- Reset edge buffers
- Stop any previous capture
- Reset TIM2 counter

3️⃣ MCU pulls DATA LOW (≈2 ms)
Protocol requirement (DHT22 datasheet):
Host pulls the bus LOW for ≥1 ms
This signals the sensor to prepare data

4️⃣ MCU releases line (AF input)
GPIO switches from open‑drain output to timer input
MCU no longer drives the line
Sensor now owns the bus

5️⃣ TIM2 captures BOTH edges
Instead of trying to decode bits on‑the‑fly:
- TIM2 captures every rising and falling edge
- Timer runs at 1 µs resolution
- BOTH edges are timestamped

This avoids:
- Missing the last falling edge
- Guessing the final bit
- Timing ambiguity at end of frame

6️⃣ ISR stores timestamps
Each capture interrupt:
- Reads CCR2
- Stores: timestamp (µs) pin level after edge (rising or falling)

Typical count:
~82 edges total
Includes ACK + 40 data bits

7️⃣ DHT22_Edges_Service()
Runs repeatedly while BUSY
Responsibilities:
- Detect silence on the bus (≈500 µs with no edges)
- Detect buffer full
- Stop capture cleanly

This is how the driver knows the sensor has finished transmitting
No guessing. No last‑bit hacks.

8️⃣ Enough edges or silence timeout?
Decision point:
- Yes → stop TIM2 capture, move to decode
-  No → keep capturing

This is instead of fragile “wait for falling edge” logic.

9️⃣ Decode HIGH pulse widths
Now decoding happens offline, not in ISR:
Process:
- Find every rising → falling pair
- Measure HIGH pulse width

DHT22 encoding:
~26–28 µs → bit 0
~70 µs → bit 1
The first HIGH (~80 µs) is the ACK → skipped.

🔟 40 bits, MSB‑first
Bits are packed exactly per spec:
- 5 bytes total
- MSB first
- No shifting errors
- No alignment problems

[ Humidity_H ][ Humidity_L ][ Temp_H ][ Temp_L ][ Checksum ]


1️⃣1️⃣ Checksum OK?
Checksum rule:
checksum == (byte0 + byte1 + byte2 + byte3) & 0xFF

❌ Fail → reject frame
✅ Pass → data is valid

This guarantees we never accept corrupted frames.

1️⃣2️⃣ Return Temperature & Humidity
Final conversion:
- Humidity = raw / 10
- Temperature = signed raw / 10

Returned to application as stable, correct values.

---

## Why this design is robust 
- Capturing facts (edges)
- Decoding after the fact
- Using checksum as authority

---

##  Usage Example

```c
float temperature, humidity;
DHT22_Status status;

DHT22_Edges_Start();

uint32_t t0 = HAL_GetTick();
do {
    DHT22_Edges_Service();
    status = DHT22_Edges_Read(&temperature, &humidity);
} while (status == DHT22_BUSY && (HAL_GetTick() - t0) < 100);

if (status == DHT22_OK) {
    printf("T=%.1f C  RH=%.1f %%\n", temperature, humidity);
}
```

---

##  Why This Works Reliably

- Interrupts guarantee precise edge timestamps
- Offline decode avoids race conditions
- No dependence on “final edge” behavior
- Checksum ensures data integrity

---

##  Limitations

CPU interrupt load proportional to number of edges (~80 per read)
Requires calling Service() periodically while BUSY

---

## Tested On

STM32F446RE @ 180 MHz
HAL drivers
DHT22 module and bare sensor


---

## License
This project is released under the MIT License.
You are free to use, modify, and share.

---

## Contributions
Pull requests and improvements are welcome.

---

## Acknowledgements
Thanks to the ST community, the SEN0137 hardware documentation,
and the open‑source embedded community.

---







