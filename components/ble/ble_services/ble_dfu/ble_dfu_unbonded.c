/**
 * Copyright (c) 2017 - 2017, Nordic Semiconductor ASA
 * 
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form, except as embedded into a Nordic
 *    Semiconductor ASA integrated circuit in a product or a software update for
 *    such product, must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 * 
 * 3. Neither the name of Nordic Semiconductor ASA nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 * 
 * 4. This software, with or without modification, must only be used with a
 *    Nordic Semiconductor ASA integrated circuit.
 * 
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 * 
 * THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "compiler_abstraction.h"
#include "nrf_dfu_svci.h"
#include "nordic_common.h"
#include "nrf_dfu_svci.h"
#include "nrf_error.h"
#include "ble_dfu.h"
#include "nrf_log.h"
#include "nrf_sdh_soc.h"

#if !defined(NRF_DFU_BLE_BUTTONLESS_SUPPORTS_BONDS) || (NRF_DFU_BLE_BUTTONLESS_SUPPORTS_BONDS == 0)

#define NRF_DFU_ADV_NAME_MAX_LENGTH     (20)


/**@brief Define functions for async interface to set new advertisement name for DFU mode.  */
NRF_SVCI_ASYNC_FUNC_DEFINE(NRF_DFU_SVCI_SET_ADV_NAME, nrf_dfu_set_adv_name, nrf_dfu_adv_name_t);

ble_dfu_buttonless_t      * mp_dfu = NULL;
static nrf_dfu_adv_name_t   m_adv_name;

void ble_dfu_buttonless_on_sys_evt(uint32_t, void * );
// Register SoC observer for the Buttonless Secure DFU service
NRF_SDH_SOC_OBSERVER(m_dfu_buttonless_soc_obs, BLE_DFU_SOC_OBSERVER_PRIO, ble_dfu_buttonless_on_sys_evt, NULL);


/**@brief Function for setting an advertisement name.
 *
 * @param[in]   adv_name    The new advertisement name.
 *
 * @retval NRF_SUCCESS      Advertisement name was successfully set.
 * @retval DFU_RSP_BUSY     Advertisement name was not set because of an ongoing operation.
 * @retval Any other errors from the SVCI interface call.
 */
static uint32_t set_adv_name(nrf_dfu_adv_name_t * p_adv_name)
{
    uint32_t err_code;

    if (mp_dfu->is_waiting_for_svci)
    {
        return DFU_RSP_BUSY;
    }

    err_code = nrf_dfu_set_adv_name(p_adv_name);
    if (err_code == NRF_SUCCESS)
    {
        // The request was accepted.
        mp_dfu->is_waiting_for_svci = true;
    }

    return err_code;
}


/**@brief Function for entering the bootloader.
 */
static uint32_t enter_bootloader()
{
    uint32_t err_code;

    if (mp_dfu->is_waiting_for_svci)
    {
        // We have an ongoing async operation. Entering bootloader mode is not possible at this time.
        err_code = ble_dfu_buttonless_resp_send(DFU_OP_ENTER_BOOTLOADER, DFU_RSP_BUSY);
        if (err_code != NRF_SUCCESS)
        {
            mp_dfu->evt_handler(BLE_DFU_EVT_RESPONSE_SEND_ERROR);
        }
        return NRF_SUCCESS;
    }

    // Set the flag indicating that we expect DFU mode.
    // This will be handled on acknowledgement of the characteristic indication.
    mp_dfu->is_waiting_for_reset = true;

    err_code = ble_dfu_buttonless_resp_send(DFU_OP_ENTER_BOOTLOADER, DFU_RSP_SUCCESS);
    if (err_code != NRF_SUCCESS)
    {
        mp_dfu->is_waiting_for_reset = false;
    }

    return err_code;
}


uint32_t nrf_dfu_svci_vector_table_set(void);
uint32_t nrf_dfu_svci_vector_table_unset(void);


uint32_t ble_dfu_buttonless_backend_init(ble_dfu_buttonless_t * p_dfu)
{
    VERIFY_PARAM_NOT_NULL(p_dfu);
    
    mp_dfu = p_dfu;
    
    return NRF_SUCCESS;
}

uint32_t ble_dfu_buttonless_async_svci_init(void)
{
    uint32_t ret_val;

    ret_val = nrf_dfu_svci_vector_table_set();
    VERIFY_SUCCESS(ret_val);

    ret_val = nrf_dfu_set_adv_name_init();
    VERIFY_SUCCESS(ret_val);

    ret_val = nrf_dfu_svci_vector_table_unset();

    return ret_val;
}


void ble_dfu_buttonless_on_sys_evt(uint32_t sys_evt, void * p_context)
{
    uint32_t err_code;

    if (!nrf_dfu_set_adv_name_is_initialized())
    {
        return;
    }

    err_code = nrf_dfu_set_adv_name_on_sys_evt(sys_evt);
    if (err_code == NRF_ERROR_BUSY)
    {
        // Async operations are ongoing.
        // No action is taken, and nothing is reported

    }
    else if (err_code == NRF_SUCCESS)
    {
        // The async operation is finished.
        // Set the flag indicating that we are waiting for indication response
        // to activate the reset.
        mp_dfu->is_waiting_for_svci = false;

        // Report back the positive response
        err_code = ble_dfu_buttonless_resp_send(DFU_OP_SET_ADV_NAME, DFU_RSP_SUCCESS);
        if (err_code != NRF_SUCCESS)
        {
            mp_dfu->evt_handler(BLE_DFU_EVT_RESPONSE_SEND_ERROR);
        }
    }
    else
    {
        // Invalid error code reported back.
        mp_dfu->is_waiting_for_svci = false;

        err_code = ble_dfu_buttonless_resp_send(DFU_OP_SET_ADV_NAME, DFU_RSP_OPERATION_FAILED);
        if (err_code != NRF_SUCCESS)
        {
            mp_dfu->evt_handler(BLE_DFU_EVT_RESPONSE_SEND_ERROR);
        }
    }
}


uint32_t ble_dfu_buttonless_char_add(ble_dfu_buttonless_t * p_dfu)
{
    ble_gatts_char_md_t char_md         = {{0}};
    ble_gatts_attr_md_t cccd_md         = {{0}};
    ble_gatts_attr_t    attr_char_value = {0};
    ble_gatts_attr_md_t attr_md         = {{0}};
    ble_uuid_t          char_uuid;


    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_md.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_md.write_perm);

    cccd_md.vloc = BLE_GATTS_VLOC_STACK;

    char_md.char_props.indicate     = 1;
    char_md.char_props.write        = 1;
    char_md.p_char_user_desc        = NULL;
    char_md.p_char_pf               = NULL;
    char_md.p_user_desc_md          = NULL;
    char_md.p_cccd_md               = &cccd_md;
    char_md.p_sccd_md               = NULL;

    char_uuid.type = p_dfu->uuid_type;
    char_uuid.uuid = BLE_DFU_BUTTONLESS_CHAR_UUID;

    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.write_perm);

    attr_md.vloc    = BLE_GATTS_VLOC_STACK;
    attr_md.rd_auth = 0;
    attr_md.wr_auth = 1;
    attr_md.vlen    = 1;

    attr_char_value.p_uuid    = &char_uuid;
    attr_char_value.p_attr_md = &attr_md;
    attr_char_value.init_len  = 0;
    attr_char_value.init_offs = 0;
    attr_char_value.max_len   = BLE_GATT_ATT_MTU_DEFAULT;
    attr_char_value.p_value   = 0;

    return sd_ble_gatts_characteristic_add(p_dfu->service_handle,
                                           &char_md,
                                           &attr_char_value,
                                           &p_dfu->control_point_char);
}


void ble_dfu_buttonless_on_ctrl_pt_write(ble_gatts_evt_write_t const * p_evt_write)
{
    uint32_t err_code;
    ble_dfu_buttonless_rsp_code_t rsp_code = DFU_RSP_OPERATION_FAILED;

    // Start executing the control point write operation
    /*lint -e415 -e416 -save "Out of bounds access"*/
    switch (p_evt_write->data[0])
    {
        case DFU_OP_ENTER_BOOTLOADER:
            err_code = enter_bootloader();
            if (err_code == NRF_SUCCESS)
            {
                rsp_code = DFU_RSP_SUCCESS;
            }
            break;

        case DFU_OP_SET_ADV_NAME:

            if(p_evt_write->data[1] > NRF_DFU_ADV_NAME_MAX_LENGTH ||
               p_evt_write->data[1] == 0                          )
            {
                // New advertisement name too short or too long.
                rsp_code = DFU_RSP_ADV_NAME_INVALID;
            }
            else
            {
                memcpy(m_adv_name.name, &p_evt_write->data[2], p_evt_write->data[1]);
                m_adv_name.len = p_evt_write->data[1];
                err_code = set_adv_name(&m_adv_name);
                if (err_code == NRF_SUCCESS)
                {
                    rsp_code = DFU_RSP_SUCCESS;
                }
            }

            break;

        default:
            rsp_code = DFU_RSP_OP_CODE_NOT_SUPPORTED;
            break;
    }
    /*lint -restore*/


    // Report back in case of error
    if (rsp_code != DFU_RSP_SUCCESS)
    {
        err_code = ble_dfu_buttonless_resp_send((ble_dfu_buttonless_op_code_t)p_evt_write->data[0], rsp_code);
        if (err_code != NRF_SUCCESS)
        {
            mp_dfu->evt_handler(BLE_DFU_EVT_RESPONSE_SEND_ERROR);
        }
    }
}

uint32_t ble_dfu_buttonless_bootloader_start_prepare(void)
{
    uint32_t err_code;

    // Indicate to main app that DFU mode is starting.
    mp_dfu->evt_handler(BLE_DFU_EVT_BOOTLOADER_ENTER_PREPARE);

    err_code = ble_dfu_buttonless_bootloader_start_finalize();
    return err_code;
}

#endif // #if defined(NRF_DFU_BOTTONLESS_SUPPORT_BOND) && (NRF_DFU_BOTTONLESS_SUPPORT_BOND == 1)
