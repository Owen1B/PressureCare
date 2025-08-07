/*
 * npwt_logic.h
 *
 *  Created on: 2024年7月22日
 *      Author: Owen
 */

#ifndef MAIN_NPWT_LOGIC_H_
#define MAIN_NPWT_LOGIC_H_

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include "npwt_core.h"


/**
 * @brief Initializes the NPWT core logic.
 *
 * @return
 *      - ESP_OK: Success
 *      - ESP_FAIL: Failed to initialize
 */
esp_err_t npwt_logic_init(void);

/**
 * @brief Deinitializes the NPWT core logic.
 *
 * @return
 *      - ESP_OK: Success
 */
esp_err_t npwt_logic_deinit(void);

/**
 * @brief Starts the NPWT control task.
 *
 * @return
 *      - ESP_OK: Success
 *      - ESP_FAIL: Failed to create task
 */
esp_err_t npwt_logic_start(void);

/**
 * @brief Stops the NPWT control task.
 *
 * @return
 *      - ESP_OK: Success
 */
esp_err_t npwt_logic_stop(void);


#endif /* MAIN_NPWT_LOGIC_H_ */

