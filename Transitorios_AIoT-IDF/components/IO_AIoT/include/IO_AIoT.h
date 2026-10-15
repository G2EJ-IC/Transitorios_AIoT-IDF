#ifndef IO_AIOT_H
#define IO_AIOT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// Initialization
// -----------------------------------------------------------------------------
void IO_AIoT_Init(void);

// -----------------------------------------------------------------------------
// Brightness & Power Management Interface
// -----------------------------------------------------------------------------

/**
 * @brief Sets the target brightness manually (from Slider).
 * @param percentage 0 to 100
 */
void IO_Set_Brillo_Manual(int32_t percentage);

/**
 * @brief Sets the suspension timeout based on Dropdown index.
 * @param index 0=Always On, 1=15s, 2=30s, 3=1min, 4=2min, 5=5min
 */
void IO_Set_Tiempo_Suspension(int32_t index);

/**
 * @brief Main logic for Power Management State Machine.
 * Must be called in the main loop.
 */
void IO_Task_Manager(void);

/**
 * @brief Gets the system uptime string (Day HH:MM:SS)
 * @param buffer Output buffer
 * @param len Buffer length
 */
void IO_Get_Uptime(char *buffer, size_t len);

#ifdef __cplusplus
}
#endif

#endif // IO_AIOT_H