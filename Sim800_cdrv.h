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
#define WAIT_FOR_COMMAND_RESPONSE_MS            3000
#define WAIT_FOR_SIM800_READY_SEND_COMMAND      5000
#define WAIT_FOR_SIM800_SEND_SMS_DELIVERY       10000
#define WIAT_FOR_CALL_RESPONSE                  5000
#define WAIT_FOR_SIM800_SEND_SMS_TRYES          3

/**
 * @brief Return codes for sim800 operations
 * 
 */
typedef uint8_t sim800_res_t;

#define SIM800_RES_OK                           ((uint8_t)0)
#define SIM800_RES_INIT_FAIL                    ((uint8_t)1)
#define SIM800_RES_SEND_COMMAND_FAIL            ((uint8_t)2)
#define SIM800_RES_SEND_SMS_FAIL                ((uint8_t)3)
#define SIM800_RES_SAVE_JSON_FAIL               ((uint8_t)4)
#define SIM800_RES_LOAD_JSON_FIAL               ((uint8_t)5)
#define SIM800_RES_PHONENUMBER_INVALID          ((uint8_t)6)
#define SIM800_RES_PHONENUMBER_NOT_FOUND        ((uint8_t)7)
#define SIM800_RES_SIMCARD_NOT_INSERTED         ((uint8_t)8)
#define SIM800_RES_REVIEVED_SMS_INVALID         ((uint8_t)9)
#define SIM800_RES_DELIVERY_REPORT_FAIL         ((uint8_t)10)
#define SIM800_RES_CALL_FAIL                    ((uint8_t)11)

/* Exported macro ------------------------------------------------------------*/    
/* Exported types ------------------------------------------------------------*/

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

    bool EnableSengingSMS;

    bool IsSending;

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
sim800_res_t fSim800_AddPhoneNumber(String PhoneNumber, bool IsAdmin);
sim800_res_t fSim800_RemovePhoneNumber(String PhoneNumber);
sim800_res_t fSim800_RemoveAllPhoneNumbers(void);
sim800_res_t fSim800_SMSSend(String PhoneNumber, String message);
sim800_res_t fSim800_SMSSendToAll(String message);
sim800_res_t fSim800_GetSimcardBalance(uint16_t *pBalance);
uint32_t fSim800_CheckCredit(void);
JsonDocument fSim800_GetPhoneNumbers(void);

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