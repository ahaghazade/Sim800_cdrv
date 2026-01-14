/**
******************************************************************************
* @file           : sim800_cdrv.h
* @brief          : Header file for WS2812 LED driver
* @note           :
* @copyright      : COPYRIGHT© 2025 DiodeGroup
******************************************************************************
* @attention
*
* <h2><center>&copy; Copyright© 2025 DiodeGroup.
* All rights reserved.</center></h2>
*
* This software is licensed under terms that can be found in the LICENSE file
* in the root directory of this software component.
* If no LICENSE file comes with this software, it is provided AS-IS.
*
******************************************************************************
* @verbatim
* @endverbatim
*/

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef CDRV_SIM800_H
#define CDRV_SIM800_H

/* Includes ------------------------------------------------------------------*/
#include <ArduinoJson.h>

#include "Sim800_defs.h"
#include "Sim800_texts.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Exported defines ----------------------------------------------------------*/
#define WAIT_FOR_COMMAND_RESPONSE_MS            2000
#define WAIT_FOR_SIM800_READY_SEND_COMMAND      2000
#define SIM800_COMMAND_ATTEMPTS                 3
#define WAIT_FOR_SIM800_SEND_SMS_DELIVERY       10000
#define WIAT_FOR_CALL_RESPONSE                  5000
#define SIM800_SEND_SMS_ATTEMPTS                3
#define SIM800_SMS_QUEUE_SIZE                   10

/**
 * @brief Return codes for sim800 operations
 * 
 */
typedef uint8_t sim800_res_t;

#define SIM800_RES_OK                           ((sim800_res_t)0)
#define SIM800_RES_INIT_FAIL                    ((sim800_res_t)1)
#define SIM800_RES_INIT_GSM_FAIL                ((sim800_res_t)2)
#define SIM800_RES_SEND_COMMAND_FAIL            ((sim800_res_t)3)
#define SIM800_RES_SEND_SMS_FAIL                ((sim800_res_t)4)
#define SIM800_RES_SAVE_JSON_FAIL               ((sim800_res_t)5)
#define SIM800_RES_LOAD_JSON_FIAL               ((sim800_res_t)6)
#define SIM800_RES_PHONENUMBER_INVALID          ((sim800_res_t)7)
#define SIM800_RES_PHONENUMBER_NOT_FOUND        ((sim800_res_t)8)
#define SIM800_RES_SIMCARD_NOT_INSERTED         ((sim800_res_t)9)
#define SIM800_RES_REVIEVED_SMS_INVALID         ((sim800_res_t)10)
#define SIM800_RES_DELIVERY_REPORT_FAIL         ((sim800_res_t)11)
#define SIM800_RES_CALL_NO_RESPONSE             ((sim800_res_t)12)
#define SIM800_RES_CALL_INITIAL_FAILD           ((sim800_res_t)13)
#define SIM800_RES_ENQUEUE_FAIL                 ((sim800_res_t)14)
#define SIM800_RES_QUEUE_EMPTY                  ((sim800_res_t)15)

/* Exported macro ------------------------------------------------------------*/    
/* Exported types ------------------------------------------------------------*/
/**
 * @brief 
 * 
*/
typedef struct {

  String PhoneNumber;

  String Text;
    
}sSmsMessage;

typedef enum {
  
  eNO_COMMAND = 0,
  eSYSTEM_COMMAND,
  eLAMP_COMMAND,
  eIP_COMMAND,
  eALARM_COMMAND,
  eMONIXIDE_COMMAND,
  eFIRE_COMMAND,
  eHUMIDITY_COMMAND,
  eTEMP_COMMAND
  
}eCommandType;

typedef enum {
  
  SMS_IDLE,
  SMS_HEADER,
  SMS_BODY

}eSmsState;

/**
 * @brief 
 * 
 */
typedef struct{
  
  int index;
  
  String phoneNumber;

  bool IsAdmin;
  
  String dateTime;

  String Massage;

}sSmsData;

/**
 * @brief 
 * 
 */
typedef struct {

  sSmsData MassageData;

  eCommandType CommandType;

}sSim800RecievedMassgeDone;

/**
 * @brief sim800 configuration structure
 * 
 */
typedef struct {

    bool Init;

    bool IsSending;

    sSmsMessage SmsQueue[SIM800_SMS_QUEUE_SIZE];
  
    uint16_t QueueHead;
  
    uint16_t QueueTail;
  
    uint16_t QueueCount;
  
    uint8_t CommandSendRetries;

    JsonDocument SavedPhoneNumbers;

    bool EnableDeliveryReport;

    Stream* ComPort;

    void(*_pfCommandEvent)(sSim800RecievedMassgeDone *e);

    sSim800RecievedMassgeDone _args;

    // void(*_pfLampEvent)(sSim800RecievedMassgeDone *e);

    // void(*_pfIpEvent)(sSim800RecievedMassgeDone *e);

    // void(*_pfAlarmEvent)(sSim800RecievedMassgeDone *e);

    // void(*_pfMonixideEvent)(sSim800RecievedMassgeDone *e);

    // void(*_pfFireEvent)(sSim800RecievedMassgeDone *e);

    // void(*_pfHumidityEvent)(sSim800RecievedMassgeDone *e);

    // void(*_pfTempEvent)(sSim800RecievedMassgeDone *e);

}sSim800;

/* Exported constants --------------------------------------------------------*/
/* Exported functions prototypes ---------------------------------------------*/
sim800_res_t fSim800_Init(void);
void fSim800_Run(void);
void fSim800_CheckInbox(void);
sim800_res_t fSim800_AddPhoneNumber(String PhoneNumber, bool IsAdmin);
sim800_res_t fSim800_RemovePhoneNumber(String PhoneNumber);
sim800_res_t fSim800_RemoveAllPhoneNumbers(void);
sim800_res_t fSim800_SMSSend(String PhoneNumber, String message);
sim800_res_t fSim800_SMSSendToAll(String message);
sim800_res_t fSim800_Call(String PhoneNumber);
sim800_res_t fSim800_GetSimcardBalance(uint16_t *pBalance);
uint32_t fSim800_CheckCredit(void);
sim800_res_t fSim800_GetPhoneNumbers(JsonDocument *pDoc);

sim800_res_t fSim800_RegisterCommandEvent(void(*fpFunc)(sSim800RecievedMassgeDone *pArgs));
// sim800_res_t fSim800_RegisterLampEvent(void(*fpFunc)(sSim800RecievedMassgeDone *e));
// sim800_res_t fSim800_RegisterIpEvent(void(*fpFunc)(sSim800RecievedMassgeDone *e));
// sim800_res_t fSim800_RegisterAlarmEvent(void(*fpFunc)(sSim800RecievedMassgeDone *e));
// sim800_res_t fSim800_RegisterMonixideEvent(void(*fpFunc)(sSim800RecievedMassgeDone *e));
// sim800_res_t fSim800_RegisterFireEvent(void(*fpFunc)(sSim800RecievedMassgeDone *e));
// sim800_res_t fSim800_RegisterHumidityEvent(void(*fpFunc)(sSim800RecievedMassgeDone *e));
// sim800_res_t fSim800_RegisterTempEvent(void(*fpFunc)(sSim800RecievedMassgeDone *e));


/* Exported variables --------------------------------------------------------*/
extern sSim800 Sim800;

#ifdef __cplusplus
}
#endif

#endif /* CDRV_SIM800_H */

/************************ © COPYRIGHT DiodeGroup *****END OF FILE****/