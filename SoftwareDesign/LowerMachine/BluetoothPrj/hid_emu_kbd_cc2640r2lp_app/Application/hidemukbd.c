/******************************************************************************

 @file       hidemukbd.c

 @brief This file contains the HID emulated keyboard sample application for use
        with the CC2650 Bluetooth Low Energy
        Protocol Stack.

 Group: CMCU, SCS
 Target Device: CC2640R2

 ******************************************************************************
 
 Copyright (c) 2011-2017, Texas Instruments Incorporated
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions
 are met:

 *  Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

 *  Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

 *  Neither the name of Texas Instruments Incorporated nor the names of
    its contributors may be used to endorse or promote products derived
    from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 ******************************************************************************
 Release Name: simplelink_cc2640r2_sdk_ble_example_pack_1_40_00_50
 Release Date: 2017-07-31 15:27:43
 *****************************************************************************/


/*********************************************************************
 * INCLUDES
 */

#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Event.h>
#include <ti/sysbios/knl/Queue.h>
#include <ti/display/Display.h>
#include <icall.h>
#include <string.h>
#include "util.h"
/* This Header file contains all BLE API and icall structure definition */
#include "icall_ble_api.h"
#include "devinfoservice.h"
#include "battservice.h"
#include "hidkbdservice.h"
#include "hiddev.h"
#include "ll_common.h"

#include "peripheral.h"
#include "board_key.h"
#include "board.h"

#include "hidemukbd.h"

/*********************************************************************
 * MACROS
 */

#define KEY_NONE                    0x00

// Selected HID LED bitmaps
#define LED_NUM_LOCK                0x01
#define LED_CAPS_LOCK               0x02

// Selected HID mouse button values
#define MOUSE_BUTTON_1              0x01
#define MOUSE_BUTTON_NONE           0x00

#ifndef TEST_HID_MANUFACTURER_CUSTOM_EQUIPMENT
//HID keyboard input report length
#define HID_KEYBOARD_IN_RPT_LEN     8
#else
#define HID_KEYBOARD_IN_RPT_LEN     21
#endif

// HID LED output report length
#define HID_LED_OUT_RPT_LEN         1

// HID mouse input report length
#define HID_MOUSE_IN_RPT_LEN        5

/*********************************************************************
 * CONSTANTS
 */

// HID idle timeout in msec; set to zero to disable timeout
//#define DEFAULT_HID_IDLE_TIMEOUT              60000
#define DEFAULT_HID_IDLE_TIMEOUT                0

// Minimum connection interval (units of 1.25ms) if automatic parameter update
// request is enabled.
#define DEFAULT_DESIRED_MIN_CONN_INTERVAL     8

// Maximum connection interval (units of 1.25ms) if automatic parameter update
// request is enabled.
#define DEFAULT_DESIRED_MAX_CONN_INTERVAL     8

// Slave latency to use if automatic parameter update request is enabled
#define DEFAULT_DESIRED_SLAVE_LATENCY         50

// Supervision timeout value (units of 10ms) if automatic parameter update
// request is enabled.
#define DEFAULT_DESIRED_CONN_TIMEOUT          500

// Whether to enable automatic parameter update request when a connection is
// formed.
#define DEFAULT_ENABLE_UPDATE_REQUEST         GAPROLE_LINK_PARAM_UPDATE_INITIATE_BOTH_PARAMS

// Connection Pause Peripheral time value (in seconds)
#define DEFAULT_CONN_PAUSE_PERIPHERAL         10

// Default GAP pairing mode
#define DEFAULT_PAIRING_MODE                  GAPBOND_PAIRING_MODE_WAIT_FOR_REQ

// Default MITM mode (TRUE to require passcode or OOB when pairing)
#define DEFAULT_MITM_MODE                     FALSE

// Default bonding mode, TRUE to bond
#define DEFAULT_BONDING_MODE                  TRUE

// Default GAP bonding I/O capabilities
#define DEFAULT_IO_CAPABILITIES               GAPBOND_IO_CAP_NO_INPUT_NO_OUTPUT

// Battery level is critical when it is less than this %
#define DEFAULT_BATT_CRITICAL_LEVEL           6

// Key bindings, can be modified to any HID value.
#ifndef CC2650_LAUNCHXL
#define KEY_UP_HID_BINDING                    HID_KEYBOARD_UP_ARROW
#define KEY_DOWN_HID_BINDING                  HID_KEYBOARD_DOWN_ARROW
#define KEY_SELECT_HID_BINDING                MOUSE_BUTTON_1
#define USE_HID_MOUSE
#endif // !CC2650_LAUNCHXL
#define KEY_LEFT_HID_BINDING                  HID_KEYBOARD_LEFT_ARROW
#define KEY_RIGHT_HID_BINDING                 HID_KEYBOARD_RIGHT_ARROW

// Task configuration
#define HIDEMUKBD_TASK_PRIORITY               1

#ifndef HIDEMUKBD_TASK_STACK_SIZE
#define HIDEMUKBD_TASK_STACK_SIZE             644
#endif

#define HIDEMUKBD_KEY_CHANGE_EVT              0x0001

// Task Events
#define HIDEMUKBD_ICALL_EVT                   ICALL_MSG_EVENT_ID // Event_Id_31
#define HIDEMUKBD_QUEUE_EVT                   UTIL_QUEUE_EVENT_ID // Event_Id_30

#define HIDEMUKBD_ALL_EVENTS                  (HIDEMUKBD_ICALL_EVT | \
                                               HIDEMUKBD_QUEUE_EVT)

/*********************************************************************
 * TYPEDEFS
 */

// App event passed from profiles.
typedef struct
{
  appEvtHdr_t hdr; // Event header
  uint8_t reportBuf[HID_KEYBOARD_IN_RPT_LEN];
} hidEmuKbdEvt_t;

/*********************************************************************
 * GLOBAL VARIABLES
 */

// Display Interface
Display_Handle dispHandle = NULL;

/*********************************************************************
 * EXTERNAL VARIABLES
 */

/*********************************************************************
 * EXTERNAL FUNCTIONS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */

// Entity ID globally used to check for source and/or destination of messages
static ICall_EntityID selfEntity;

// Event globally used to post local events and pend on system and
// local events.
static ICall_SyncHandle syncEvent;

// Queue object used for app messages
static Queue_Struct appMsg;
static Queue_Handle appMsgQueue;

// Task configuration
Task_Struct hidEmuKbdTask;
Char hidEmuKbdTaskStack[HIDEMUKBD_TASK_STACK_SIZE];

// GAP Profile - Name attribute for SCAN RSP data
static uint8_t scanData[] =
{
 /***************************************************************************/
#if defined(CC2640R2_LAUNCHXL)  //BTP_101 HID Beta Version
    // complete name
    0x19,   // length of this data
    GAP_ADTYPE_LOCAL_NAME_COMPLETE,
    'B',
    'T',
    'P',
    '_',
    '1',
    '0',
    '1',
    ' ',
    'H',
    'I',
    'D',
    ' ',
    'B',
    'e',
    't',
    'a',
    ' ',
    'V',
    'e',
    'r',
    's',
    'i',
    'o',
    'n',
#else           //"BTP_101 HID Beta"
    0x11,                             // length of this data
    GAP_ADTYPE_LOCAL_NAME_COMPLETE,   // AD Type = Complete local name
    'B',
    'T',
    'P',
    '_',
    '1',
    '0',
    '1',
    ' ',
    'H',
    'I',
    'D',
    ' ',
    'B',
    'e',
    't',
    'a',
#endif
};

// Advertising data
static uint8_t advData[] =
{
  // flags
  0x02,   // length of this data
  GAP_ADTYPE_FLAGS,
  GAP_ADTYPE_FLAGS_LIMITED | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,

  // appearance
  0x03,   // length of this data
  GAP_ADTYPE_APPEARANCE,
//  LO_UINT16(GAP_APPEARE_HID_KEYBOARD),
//  HI_UINT16(GAP_APPEARE_HID_KEYBOARD),
    LO_UINT16(GAP_APPEARE_GENERIC_HID),
    HI_UINT16(GAP_APPEARE_GENERIC_HID),

  // service UUIDs
  0x05,   // length of this data
  GAP_ADTYPE_16BIT_MORE,
  LO_UINT16(HID_SERV_UUID),
  HI_UINT16(HID_SERV_UUID),
  LO_UINT16(BATT_SERV_UUID),
  HI_UINT16(BATT_SERV_UUID)
};

//// Device name attribute value
//static CONST uint8_t attDeviceName[GAP_DEVICE_NAME_LEN] = "HID Keyboard";
#if defined(CC2640R2_LAUNCHXL)
static uint8_t attDeviceName[GAP_DEVICE_NAME_LEN] = "BTP_101 HID Beta Version";
#else
static uint8_t attDeviceName[GAP_DEVICE_NAME_LEN] = "BTP_101 HID Beta";
#endif

// HID Dev configuration
static hidDevCfg_t hidEmuKbdCfg =
{
  DEFAULT_HID_IDLE_TIMEOUT,   // Idle timeout
  HID_KBD_FLAGS               // HID feature flags
};

#ifdef USE_HID_MOUSE
// TRUE if boot mouse enabled
static uint8_t hidBootMouseEnabled = FALSE;
#endif // USE_HID_MOUSE

/*********************************************************************
 * LOCAL FUNCTIONS
 */

// Application task and event processing.
static void HidEmuKbd_init(void);
static void HidEmuKbd_taskFxn(UArg a0, UArg a1);
static void HidEmuKbd_processAppMsg(hidEmuKbdEvt_t *pMsg);
static void HidEmuKbd_processStackMsg(ICall_Hdr *pMsg);
static void HidEmuKbd_processGattMsg(gattMsgEvent_t *pMsg);
static uint8_t HidEmuKbd_enqueueMsg(uint16_t event, uint8_t state);

// Key press.
static void HidEmuKbd_keyPressHandler(uint8_t keys);
static void HidEmuKbd_handleKeys(uint8_t shift, uint8_t *pbuf);

// HID reports.
static void HidEmuKbd_sendReport(uint8_t* pbuf);
#ifdef USE_HID_MOUSE
static void HidEmuKbd_sendMouseReport(uint8_t buttons);
#endif // USE_HID_MOUSE
static uint8_t HidEmuKbd_receiveReport(uint8_t len, uint8_t *pData);
static uint8_t HidEmuKbd_reportCB(uint8_t id, uint8_t type, uint16_t uuid,
                                  uint8_t oper, uint16_t *pLen, uint8_t *pData);
static void HidEmuKbd_hidEventCB(uint8_t evt);

/*********************************************************************
 * PROFILE CALLBACKS
 */

static hidDevCB_t hidEmuKbdHidCBs =
{
  HidEmuKbd_reportCB,
  HidEmuKbd_hidEventCB,
  NULL
};

/*********************************************************************
 * PUBLIC FUNCTIONS
 */

/*********************************************************************
 * @fn      HidEmuKbd_createTask
 *
 * @brief   Task creation function for the HID emulated keyboard.
 *
 * @param   none
 *
 * @return  none
 */
void HidEmuKbd_createTask(void)
{
  Task_Params taskParams;

  // Configure task
  Task_Params_init(&taskParams);
  taskParams.stack = hidEmuKbdTaskStack;
  taskParams.stackSize = HIDEMUKBD_TASK_STACK_SIZE;
  taskParams.priority = HIDEMUKBD_TASK_PRIORITY;

  Task_construct(&hidEmuKbdTask, HidEmuKbd_taskFxn, &taskParams, NULL);
}

/*********************************************************************
 * @fn      HidEmuKbd_init
 *
 * @brief   Initialization function for the HidEmuKbd App Task.
 *          This is called during initialization and should contain
 *          any application specific initialization (ie. hardware
 *          initialization/setup, table initialization, power up
 *          notification ...).
 *
 * @param   none
 *
 * @return  none
 */
void HidEmuKbd_init(void)
{
	// ******************************************************************
  // N0 STACK API CALLS CAN OCCUR BEFORE THIS CALL TO ICall_registerApp
  // ******************************************************************
  // Register the current thread as an ICall dispatcher application
  // so that the application can send and receive messages.
  ICall_registerApp(&selfEntity, &syncEvent);

  HCI_EXT_SetTxPowerCmd(HCI_EXT_TX_POWER_5_DBM);   //���÷��书��
  // Hard code the DB Address till CC2650 board gets its own IEEE address
  //uint8 bdAddress[B_ADDR_LEN] = { 0x22, 0x22, 0x22, 0x22, 0x22, 0x5A };
  //HCI_EXT_SetBDADDRCmd(bdAddress);

  // Set device's Sleep Clock Accuracy
  //HCI_EXT_SetSCACmd(40);

  // Create an RTOS queue for message from profile to be sent to app.
  appMsgQueue = Util_constructQueue(&appMsg);

  // Setup the GAP
  VOID GAP_SetParamValue(TGAP_CONN_PAUSE_PERIPHERAL,
                         DEFAULT_CONN_PAUSE_PERIPHERAL);

  // Setup the GAP Peripheral Role Profile
  {
    uint8_t initial_advertising_enable = TRUE;

    // By setting this to zero, the device will go into the waiting state after
    // being discoverable for 30.72 second, and will not being advertising again
    // until the enabler is set back to TRUE
    uint16_t gapRole_AdvertOffTime = 0;

    uint8_t enable_update_request = DEFAULT_ENABLE_UPDATE_REQUEST;
    uint16_t desired_min_interval = DEFAULT_DESIRED_MIN_CONN_INTERVAL;
    uint16_t desired_max_interval = DEFAULT_DESIRED_MAX_CONN_INTERVAL;
    uint16_t desired_slave_latency = DEFAULT_DESIRED_SLAVE_LATENCY;
    uint16_t desired_conn_timeout = DEFAULT_DESIRED_CONN_TIMEOUT;

    // Set the GAP Role Parameters
    GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t),
                         &initial_advertising_enable);
    GAPRole_SetParameter(GAPROLE_ADVERT_OFF_TIME, sizeof(uint16_t),
                         &gapRole_AdvertOffTime);

    GAPRole_SetParameter(GAPROLE_ADVERT_DATA, sizeof(advData), advData);
    GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(scanData), scanData);

    GAPRole_SetParameter(GAPROLE_PARAM_UPDATE_ENABLE, sizeof(uint8_t),
                         &enable_update_request);
    GAPRole_SetParameter(GAPROLE_MIN_CONN_INTERVAL, sizeof(uint16_t),
                         &desired_min_interval);
    GAPRole_SetParameter(GAPROLE_MAX_CONN_INTERVAL, sizeof(uint16_t),
                         &desired_max_interval);
    GAPRole_SetParameter(GAPROLE_SLAVE_LATENCY, sizeof(uint16_t),
                         &desired_slave_latency);
    GAPRole_SetParameter(GAPROLE_TIMEOUT_MULTIPLIER, sizeof(uint16_t),
                         &desired_conn_timeout);
  }

  // Set the GAP Characteristics
  GGS_SetParameter(GGS_DEVICE_NAME_ATT, GAP_DEVICE_NAME_LEN,
                   (void *)attDeviceName);

  // Setup the GAP Bond Manager
  {
    uint8_t pairMode = DEFAULT_PAIRING_MODE;
    uint8_t mitm = DEFAULT_MITM_MODE;
    uint8_t ioCap = DEFAULT_IO_CAPABILITIES;
    uint8_t bonding = DEFAULT_BONDING_MODE;

    GAPBondMgr_SetParameter(GAPBOND_PAIRING_MODE, sizeof(uint8_t), &pairMode);
    GAPBondMgr_SetParameter(GAPBOND_MITM_PROTECTION, sizeof(uint8_t), &mitm);
    GAPBondMgr_SetParameter(GAPBOND_IO_CAPABILITIES, sizeof(uint8_t), &ioCap);
    GAPBondMgr_SetParameter(GAPBOND_BONDING_ENABLED, sizeof(uint8_t), &bonding);
  }

  // Setup Battery Characteristic Values
  {
    uint8_t critical = DEFAULT_BATT_CRITICAL_LEVEL;

    Batt_SetParameter(BATT_PARAM_CRITICAL_LEVEL, sizeof (uint8_t), &critical);
  }

  // Set up HID keyboard service
  HidKbd_AddService();

  // Register for HID Dev callback
  HidDev_Register(&hidEmuKbdCfg, &hidEmuKbdHidCBs);

  // Start the GAP Role and Register the Bond Manager.
  HidDev_StartDevice();

  // Initialize keys on SmartRF06EB.
  Board_initKeys(HidEmuKbd_keyPressHandler);

  // Register with GAP for HCI/Host messages
  GAP_RegisterForMsgs(selfEntity);

#if !defined (USE_LL_CONN_PARAM_UPDATE)
  // Get the currently set local supported LE features
  // The HCI will generate an HCI event that will get received in the main
  // loop
  HCI_LE_ReadLocalSupportedFeaturesCmd();
#endif // !defined (USE_LL_CONN_PARAM_UPDATE)
  
  // Use default data Tx / Rx data length and times
  HCI_EXT_SetMaxDataLenCmd(LL_MIN_LINK_DATA_LEN, LL_MIN_LINK_DATA_TIME, LL_MIN_LINK_DATA_LEN, LL_MIN_LINK_DATA_TIME);
}

/*********************************************************************
 * @fn      HidEmuKbd_taskFxn
 *
 * @brief   HidEmuKbd Application Task event processor.  This function
 *          is called to process all events for the task.  Events
 *          include timers, messages and any other user defined events.
 *
 * @param   a0, a1 - not used.
 *
 * @return  none
 */
void HidEmuKbd_taskFxn(UArg a0, UArg a1)
{
  // Initialize the application.
  HidEmuKbd_init();

  // Application main loop.
  for (;;)
  {
    uint32_t events;

    events = Event_pend(syncEvent, Event_Id_NONE, HIDEMUKBD_ALL_EVENTS,
                        ICALL_TIMEOUT_FOREVER);

    if (events)
    {
      ICall_EntityID dest;
      ICall_ServiceEnum src;
      ICall_HciExtEvt *pMsg = NULL;

      if (ICall_fetchServiceMsg(&src, &dest,
                                (void **)&pMsg) == ICALL_ERRNO_SUCCESS)
      {
        if ((src == ICALL_SERVICE_CLASS_BLE) && (dest == selfEntity))
        {
          // Process inter-task message
          HidEmuKbd_processStackMsg((ICall_Hdr *)pMsg);
        }

        if (pMsg)
        {
          ICall_freeMsg(pMsg);
        }
      }

      // If RTOS queue is not empty, process app message.
      if (events & HIDEMUKBD_QUEUE_EVT)
      {
        while (!Queue_empty(appMsgQueue))
        {
          hidEmuKbdEvt_t *pMsg = (hidEmuKbdEvt_t *)Util_dequeueMsg(appMsgQueue);
          if (pMsg)
          {
            // Process message.
            HidEmuKbd_processAppMsg(pMsg);

            // Free the space from the message.
            ICall_free(pMsg);
          }
        }
      }
    }
  }
}

/*********************************************************************
 * @fn      HidEmuKbd_processStackMsg
 *
 * @brief   Process an incoming stack message.
 *
 * @param   pMsg - message to process
 *
 * @return  none
 */
static void HidEmuKbd_processStackMsg(ICall_Hdr *pMsg)
{
  switch (pMsg->event)
  {
    case GATT_MSG_EVENT:
      HidEmuKbd_processGattMsg((gattMsgEvent_t *) pMsg);
      break;

    case HCI_GAP_EVENT_EVENT:
      {

        // Process HCI message
        switch(pMsg->status)
        {
          case HCI_COMMAND_COMPLETE_EVENT_CODE:
            // Process HCI Command Complete Event
            {

#if !defined (USE_LL_CONN_PARAM_UPDATE)
              // This code will disable the use of the LL_CONNECTION_PARAM_REQ
              // control procedure (for connection parameter updates, the
              // L2CAP Connection Parameter Update procedure will be used
              // instead). To re-enable the LL_CONNECTION_PARAM_REQ control
              // procedures, define the symbol USE_LL_CONN_PARAM_UPDATE

              // Parse Command Complete Event for opcode and status
              hciEvt_CmdComplete_t* command_complete = (hciEvt_CmdComplete_t*) pMsg;
              uint8_t   pktStatus = command_complete->pReturnParam[0];

              //find which command this command complete is for
              switch (command_complete->cmdOpcode)
              {
                case HCI_LE_READ_LOCAL_SUPPORTED_FEATURES:
                  {
                    if (pktStatus == SUCCESS)
                    {
                      uint8_t featSet[8];

                      // get current feature set from received event (bits 1-9 of
                      // the returned data
                      memcpy( featSet, &command_complete->pReturnParam[1], 8 );

                      // Clear bit 1 of byte 0 of feature set to disable LL
                      // Connection Parameter Updates
                      CLR_FEATURE_FLAG( featSet[0], LL_FEATURE_CONN_PARAMS_REQ );

                      // Update controller with modified features
                      HCI_EXT_SetLocalSupportedFeaturesCmd( featSet );
                    }
                  }
                  break;

                default:
                  //do nothing
                  break;
              }
#endif // !defined (USE_LL_CONN_PARAM_UPDATE)

            }
            break;

          default:
            break;
        }
      }
      break;

    default:
      // Do nothing
      break;
  }
}

/*********************************************************************
 * @fn      HidEmuKbd_processGattMsg
 *
 * @brief   Process GATT messages
 *
 * @return  none
 */
static void HidEmuKbd_processGattMsg(gattMsgEvent_t *pMsg)
{
  GATT_bm_free(&pMsg->msg, pMsg->method);
}

/*********************************************************************
 * @fn      HidEmuKbd_processAppMsg
 *
 * @brief   Process an incoming task message.
 *
 * @param   pMsg - message to process
 *
 * @return  none
 */
static void HidEmuKbd_processAppMsg(hidEmuKbdEvt_t *pMsg)
{
  switch (pMsg->hdr.event)
  {
    case HIDEMUKBD_KEY_CHANGE_EVT:
      HidEmuKbd_handleKeys(0, pMsg->reportBuf);
      break;

    default:
      //Do nothing.
      break;
  }
}

/*********************************************************************
 * @fn      HidKEmukbd_keyPressHandler
 *
 * @brief   Key event handler function.
 *
 * @param   a0 - ignored
 *
 * @return  none
 */
static void HidEmuKbd_keyPressHandler(uint8_t keys)
{
  // Enqueue the event.
  HidEmuKbd_enqueueMsg(HIDEMUKBD_KEY_CHANGE_EVT, keys);
}

/*********************************************************************
 * @fn      HidEmuKbd_handleKeys
 *
 * @brief   Handles all key events for this device.
 *
 * @param   shift - true if in shift/alt.
 * @param   keys - bit field for key events. Valid entries:
 *                 KEY_UP
 *                 KEY_RIGHT
 *
 * @return  none
 */
static void HidEmuKbd_handleKeys(uint8_t shift, uint8_t * pBuf)
{
  (void)shift;  // Intentionally unreferenced parameter

    // Key Release.
    // NB: releasing a key press will not propagate a signal to this function,
    // so a "key release" is reported immediately afterwards here.
    HidEmuKbd_sendReport(pBuf);
}

/*********************************************************************
 * @fn      HidEmuKbd_sendReport
 *
 * @brief   Build and send a HID keyboard report.
 *
 * @param   keycode - HID keycode.
 *
 * @return  none
 */
static void HidEmuKbd_sendReport(uint8_t* pbuf)
{
  HidDev_Report(HID_RPT_ID_KEY_IN, HID_REPORT_TYPE_INPUT,
                HID_KEYBOARD_IN_RPT_LEN, pbuf);
  piSerialTransfer->SendTransferCMPMsgToMCU();
}

#ifdef USE_HID_MOUSE
/*********************************************************************
 * @fn      HidEmuKbd_sendMouseReport
 *
 * @brief   Build and send a HID mouse report.
 *
 * @param   buttons - Mouse button code
 *
 * @return  none
 */
static void HidEmuKbd_sendMouseReport(uint8_t buttons)
{
  uint8_t buf[HID_MOUSE_IN_RPT_LEN];

  buf[0] = buttons;   // Buttons
  buf[1] = 0;         // X
  buf[2] = 0;         // Y
  buf[3] = 0;         // Wheel
  buf[4] = 0;         // AC Pan

  HidDev_Report(HID_RPT_ID_MOUSE_IN, HID_REPORT_TYPE_INPUT,
                HID_MOUSE_IN_RPT_LEN, buf);
}
#endif // USE_HID_MOUSE

/*********************************************************************
 * @fn      HidEmuKbd_receiveReport
 *
 * @brief   Process an incoming HID keyboard report.
 *
 * @param   len - Length of report.
 * @param   pData - Report data.
 *
 * @return  status
 */
static uint8_t HidEmuKbd_receiveReport(uint8_t len, uint8_t *pData)
{
	if (len != HID_KEYBOARD_IN_RPT_LEN){
		return ATT_ERR_INVALID_VALUE_SIZE;
	}
	piSerialTransfer->SendMsgToMCU(pData, len);
	return SUCCESS;
}

/*********************************************************************
 * @fn      HidEmuKbd_reportCB
 *
 * @brief   HID Dev report callback.
 *
 * @param   id - HID report ID.
 * @param   type - HID report type.
 * @param   uuid - attribute uuid.
 * @param   oper - operation:  read, write, etc.
 * @param   len - Length of report.
 * @param   pData - Report data.
 *
 * @return  GATT status code.
 */
static uint8_t HidEmuKbd_reportCB(uint8_t id, uint8_t type, uint16_t uuid,
                                  uint8_t oper, uint16_t *pLen, uint8_t *pData)
{
  uint8_t status = SUCCESS;

  // Write
  if (oper == HID_DEV_OPER_WRITE)
  {
    if (uuid == REPORT_UUID)
    {
      // Process write to LED output report; ignore others
      if (type == HID_REPORT_TYPE_OUTPUT)
      {
        status = HidEmuKbd_receiveReport(*pLen, pData);
      }
    }

    if (status == SUCCESS)
    {
      status = HidKbd_SetParameter(id, type, uuid, *pLen, pData);
    }
  }
  // Read
  else if (oper == HID_DEV_OPER_READ)
  {
    uint8_t len;

    status = HidKbd_GetParameter(id, type, uuid, &len, pData);
    if (status == SUCCESS)
    {
      *pLen = len;
    }
  }
  // Notifications enabled
  else if (oper == HID_DEV_OPER_ENABLE)
  {
    if (id == HID_RPT_ID_MOUSE_IN && type == HID_REPORT_TYPE_INPUT)
    {
#ifdef USE_HID_MOUSE
      hidBootMouseEnabled = TRUE;
#endif // USE_HID_MOUSE
    }
  }
  // Notifications disabled
  else if (oper == HID_DEV_OPER_DISABLE)
  {
    if (id == HID_RPT_ID_MOUSE_IN && type == HID_REPORT_TYPE_INPUT)
    {
#ifdef USE_HID_MOUSE
      hidBootMouseEnabled = FALSE;
#endif // USE_HID_MOUSE
    }
  }

  return status;
}

/*********************************************************************
 * @fn      HidEmuKbd_hidEventCB
 *
 * @brief   HID Dev event callback.
 *
 * @param   evt - event ID.
 *
 * @return  HID response code.
 */
static void HidEmuKbd_hidEventCB(uint8_t evt)
{
  // Process enter/exit suspend or enter/exit boot mode
  return;
}

/*********************************************************************
 * @fn      HidEmuKbd_enqueueMsg
 *
 * @brief   Creates a message and puts the message in RTOS queue.
 *
 * @param   event  - message event.
 * @param   state  - message state.
 *
 * @return  TRUE or FALSE
 */
static uint8_t HidEmuKbd_enqueueMsg(uint16_t event, uint8_t state)
{
  hidEmuKbdEvt_t *pMsg;

  // Create dynamic pointer to message.
  if (pMsg = ICall_malloc(sizeof(hidEmuKbdEvt_t)))
  {
    pMsg->hdr.event = event;
    pMsg->hdr.state = state;

    // Enqueue the message.
    return Util_enqueueMsg(appMsgQueue, syncEvent, (uint8_t *)pMsg);
  }

  return FALSE;
}

/*********************************************************************
 * @fn      HidEmuKbd_enqueueMsg
 *
 * @brief   Creates a message and puts the message in RTOS queue.
 *
 * @param   event  - message event.
 * @param   state  - message state.
 *
 * @return  TRUE or FALSE
 */
uint8_t HidEmuKbd_SendMsgtoBLERF(uint8_t * pbuf, uint8_t size)
{
  hidEmuKbdEvt_t *pMsg;

  // Create dynamic pointer to message.
  if (pMsg = ICall_malloc(sizeof(hidEmuKbdEvt_t)))
  {
    pMsg->hdr.event = HIDEMUKBD_KEY_CHANGE_EVT;
    pMsg->hdr.state = 0x00;
	memcpy(pMsg->reportBuf, pbuf, size);

    // Enqueue the message.
    return Util_enqueueMsg(appMsgQueue, syncEvent, (uint8_t *)pMsg);
  }

  return FALSE;
}


/*********************************************************************
*********************************************************************/
