# SEN0137 DHT22 Driver (Interrupt-Based Edge Capture)

Robust STM32 HAL driver for the **SEN0137 / DHT22 / AM2302** temperature and humidity sensor using **timer input capture + interrupts** and **offline edge decoding**.

This project demonstrates a reliable, timing-accurate way to read DHT22 sensors without bit-banging delays or blocking loops.

---

## ✅ Features

- ✅ Uses **hardware timer input capture** (µs resolution)
- ✅ Captures **all signal edges**, decodes offline
- ✅ No reliance on fragile “last falling edge”
- ✅ Accurate decoding of DHT22 pulse-width protocol
- ✅ Checksum-validated temperature & humidity
- ✅ Non-blocking, interrupt-driven design
- ✅ Suitable for production firmware

---

## 📦 Hardware

| Component | Description |
|---------|------------|
| Sensor | SEN0137 / DHT22 / AM2302 |
| MCU | STM32F446RE (tested) |
| Timer | TIM2 |
| GPIO | PA1 (TIM2_CH2) |
| Pull-up | 4.7kΩ–10kΩ on DATA line |

---

## 🧠 How It Works (High-Level)

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

## Project Structure
```
X-Nucleo-GFX01M2/
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

Keil users: be sure to add  
`Drivers/Custom/`  
to the **include paths in uvision**
