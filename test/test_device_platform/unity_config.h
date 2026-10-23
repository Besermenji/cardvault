// Custom Unity output for the on-device suite. PlatformIO's default calls
// Serial.end() when the run completes, which on the ESP32-S3's native USB
// switches the USB port off: the Cardputer then vanishes from the computer
// until it is power-cycled. Here output is flushed but USB stays up.
#ifndef UNITY_CONFIG_H
#define UNITY_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

void unityOutputStart(unsigned long baudrate);
void unityOutputChar(unsigned int c);
void unityOutputFlush(void);
void unityOutputComplete(void);

#define UNITY_OUTPUT_START() unityOutputStart(115200)
#define UNITY_OUTPUT_CHAR(c) unityOutputChar(c)
#define UNITY_OUTPUT_FLUSH() unityOutputFlush()
#define UNITY_OUTPUT_COMPLETE() unityOutputComplete()

#ifdef __cplusplus
}
#endif

#endif
