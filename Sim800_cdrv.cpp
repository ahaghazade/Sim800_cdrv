/**
 ******************************************************************************
 * @file           : sim800_cdrv.c
 * @brief          :
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2025 DiodeGroup.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component
 *
 *
 ******************************************************************************
 * @verbatim
 * @endverbatim
 */

/* Includes ------------------------------------------------------------------*/
#include "Sim800_cdrv.h"

#include <SPIFFS.h>
#include <Arduino.h>

/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
#define fNotifyEventCommand_() \
	if(me->_pfCommandEvent != NULL) { \
		me->_pfCommandEvent(me, &(me->_args)); \
	}
/* Private typedef -----------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
const char* SavedPhoneNumbersPath = "/PhoneNumbers.json";

/* Private function prototypes -----------------------------------------------*/
static sim800_res_t fLoadPhoneNumbers(sSim800 * const me, String Path);
static sim800_res_t fSavePhoneNumbers(sSim800 * const me, String Path);
static sim800_res_t fNormalizedPhoneNumber(String PhoneNumber, String *Normalized);
static sim800_res_t fGSM_Init(sSim800 * const me);
static sim800_res_t fInbox_Read();
static sim800_res_t fInbox_Clear();
static sim800_res_t fRecivedSms_Parse(sSim800 * const me, const String *pLine);
static sim800_res_t fRecivedSms_CheckCommand(sSim800 * const me);

/* Variables -----------------------------------------------------------------*/

/*
╔═════════════════════════════════════════════════════════════════════════════════╗
║                          ##### Exported Functions #####                         ║
╚═════════════════════════════════════════════════════════════════════════════════╝*/
/**
 * @brief 
 * 
 * @param me 
 * @return sim800_res_t 
 */
sim800_res_t fSim800_Init(sSim800 * const me) {

  if(me == NULL) {
    return SIM800_RES_INIT_FAIL;
  }

  me->Init = false;

  if (!SPIFFS.begin(true)) { 
    return SIM800_RES_INIT_FAIL;
  }

  if(fLoadPhoneNumbers(me, SavedPhoneNumbersPath) != SIM800_RES_OK) {
    return SIM800_RES_INIT_FAIL;
  }

  if(fGSM_Init(me) != SIM800_RES_OK) {
    return SIM800_RES_INIT_FAIL;
  }

  me->EnableSengingSMS = true;
  me->Init = true;
  me->IsSending = false;

  return SIM800_RES_OK;
}

/**
 * @brief 
 * 
 * @param me 
 */
void fSim800_Run(sSim800 * const me) {

  if (!me || !me->Init) return;

  Serial.println("Checking inbox...");
  // Send AT+CMGL manually
  me->ComPort->println("AT+CMGL=\"REC UNREAD\"");

  unsigned long startTime = millis();
  while (millis() - startTime < WAIT_FOR_COMMAND_RESPONSE_MS) {
    
    while (me->ComPort->available() > 0) {

      String line = me->ComPort->readStringUntil('\n');
      line.trim();

      // Ignore empty pLine->
      if (line.length() == 0) continue;

      // Ignore unsolicited +CMTI messages
      if (line.startsWith("+CMTI:")) continue;

      // Process SMS header
      if (line.startsWith("+CMGL:")) {
        // +CMGL: 1,"REC UNREAD","+989123456789","","25/09/12,21:32:15+14"
        Serial.println("New Unreaded msg");
        Serial.println(line);
        
        if(fRecivedSms_Parse(me, &line) != SIM800_RES_OK) {
          return;
        }

        // Next pLine->is the message body
        String message = me->ComPort->readStringUntil('\n');
        message.trim();

        me->_args.MassageData.Massage = message;

        Serial.printf("SMS (index %d) from %s : %s\n", me->_args.MassageData.index, me->_args.MassageData.phoneNumber , me->_args.MassageData.Massage.c_str());

        // Delete after reading
        String deleteCmd = "AT+CMGD=" + String(me->_args.MassageData.index) + ",0";
        fSim800_SendCommand(me, deleteCmd, "OK");

        if(fRecivedSms_CheckCommand(me) != SIM800_RES_OK) {
          return;
        }
      }
    }
  }
}

/**
 * @brief 
 * 
 * @param me 
 * @param PhoneNumber 
 * @param IsAdmin 
 * @return sim800_res_t 
 */
sim800_res_t fSim800_AddPhoneNumber(sSim800 * const me, String PhoneNumber, bool IsAdmin) {

  if(me == NULL || !me->Init) {
    return SIM800_RES_INIT_FAIL;
  }

  String NormalizedPhoneNumber;
  if(fNormalizedPhoneNumber(PhoneNumber, &NormalizedPhoneNumber) != SIM800_RES_OK) {
    return SIM800_RES_PHONENUMBER_INVALID;
  }

  me->SavedPhoneNumbers[NormalizedPhoneNumber] = IsAdmin? 1 : 0;
  fSavePhoneNumbers(me, SavedPhoneNumbersPath);

  return SIM800_RES_OK;
}

/**
 * @brief 
 * 
 * @param me 
 * @param PhoneNumber 
 * @return sim800_res_t 
 */
sim800_res_t fSim800_RemovePhoneNumber(sSim800 * const me, String PhoneNumber) {

  if (me == NULL || !me->Init) {
    return SIM800_RES_INIT_FAIL;
  }

  String NormalizedPhoneNumber;
  if (fNormalizedPhoneNumber(PhoneNumber, &NormalizedPhoneNumber) != SIM800_RES_OK) {
    return SIM800_RES_PHONENUMBER_INVALID;
  }

  if (!me->SavedPhoneNumbers.containsKey(NormalizedPhoneNumber)) {
    return SIM800_RES_PHONENUMBER_NOT_FOUND;
  }

  me->SavedPhoneNumbers.remove(NormalizedPhoneNumber);
  fSavePhoneNumbers(me, SavedPhoneNumbersPath);

  return SIM800_RES_OK;
}

/**
 * @brief 
 * 
 * @param me 
 * @param PhoneNumber 
 * @return sim800_res_t 
 */
sim800_res_t fSim800_RemoveAllPhoneNumbers(sSim800 * const me) {

  if(me == NULL || !me->Init) {
    return SIM800_RES_INIT_FAIL;
  }

  me->SavedPhoneNumbers.clear();

  return SIM800_RES_OK;
}

/**
 * @brief 
 * 
 * @param me 
 * @param PhoneNumber 
 * @param Text 
 * @return sim800_res_t 
 */
sim800_res_t fSim800_SendSMS(sSim800 * const me, String PhoneNumber, String Text) {

  if(fSim800_SendCommand(me,SET_TEXT_MODE, ATOK) != SIM800_RES_OK) {
    return SIM800_RES_SEND_COMMAND_FAIL;
  }
  String NormalizedPhoneNum;
  if(fNormalizedPhoneNumber(PhoneNumber, &NormalizedPhoneNum) != SIM800_RES_OK) {
    return SIM800_RES_PHONENUMBER_INVALID;
  }

  String TargetPhoneNumber = String(SET_PHONE_NUM) + "+98" + NormalizedPhoneNum.substring(1) + "\"";
  if(fSim800_SendCommand(me, TargetPhoneNumber, SEND_SMS_START)) {
    return SIM800_RES_SEND_COMMAND_FAIL;
  }

  me->IsSending = true;
  me->ComPort->print(Text);
  me->ComPort->write(SEND_SMS_END);

  delay(10);

  if(fSim800_SendCommand(me, AT, ATOK)) {
    return SIM800_RES_SEND_COMMAND_FAIL;
  }

	return SIM800_RES_OK;
}

/**
 * @brief 
 * 
 * @param me 
 * @param Command 
 * @param DesiredResponse 
 * @return sim800_res_t 
 */
sim800_res_t fSim800_SendCommand(sSim800 * const me, String Command, String DesiredResponse) {

  me->IsSending = true;
  bool commandResponsed = false;
  int commandTries = 0;

  while(!commandResponsed && commandTries < me->CommandSendRetries) {
    
    commandTries++;
    
    Serial.printf("\nSending %s  ...(%d) -- desired response: %s\n", Command.c_str(), commandTries, DesiredResponse);

    me->ComPort->println(Command);
    unsigned long startTime = millis();

    while(!commandResponsed && millis() - startTime < WAIT_FOR_COMMAND_RESPONSE_MS) {

      while(me->ComPort->available() > 0) {  

        String line = me->ComPort->readString();

        Serial.println("==== readed data =====");  
        Serial.println(line);
        Serial.print("line indexOf(DesiredRes): ");
        Serial.println(line.indexOf(DesiredResponse));

        if (line.indexOf(DesiredResponse) != -1) {

          Serial.printf("Request %s : Success.\n", Command.c_str());
          commandResponsed = true;
          break;
        }
      }
    }
  }

  me->IsSending = false;

  if(commandResponsed == true) {
    return SIM800_RES_OK;
  } else {
	return SIM800_RES_SEND_COMMAND_FAIL;
  }
}

/**
 * @brief 
 * 
 * @param me 
 * @param pBalance 
 * @return sim800_res_t 
 */
sim800_res_t fSim800_GetSimcardBalance(sSim800 * const me, uint16_t *pBalance) {


  return SIM800_RES_OK;
}

/**
 * @brief 
 * 
 * @param me 
 * @param fpFunc 
 * @return uint8_t 
 */
sim800_res_t fSim800_RegisterCommandEvent(sSim800 * const me, void(*fpFunc)(void *sender, sSim800RecievedMassgeDone *pArgs)) {
	
	if(me == NULL || fpFunc == NULL) {
    return SIM800_RES_INIT_FAIL;
  }

	me->_pfCommandEvent = fpFunc;
	
	return SIM800_RES_OK;
}


/*
╔═════════════════════════════════════════════════════════════════════════════════╗
║                            ##### Private Functions #####                        ║
╚═════════════════════════════════════════════════════════════════════════════════╝*/
/**
 * @brief 
 * 
 * @param me 
 * @param Path 
 * @return sim800_res_t 
 */
static sim800_res_t fSavePhoneNumbers(sSim800 * const me, String Path) {

  File file = SPIFFS.open(Path, FILE_WRITE);
  if(!file) {
    return SIM800_RES_LOAD_JSON_FIAL;
  }

  serializeJson(me->SavedPhoneNumbers, file);
  file.close();

  return SIM800_RES_OK;
}

/**
 * @brief 
 * 
 * @param JsonDoc 
 * @param Path 
 * @return sim800_res_t 
 */
static sim800_res_t fLoadPhoneNumbers(sSim800 * const me, String Path) {

  File file = SPIFFS.open(Path, FILE_READ);
  if(!file) {
    return SIM800_RES_LOAD_JSON_FIAL;
  }
  deserializeJson(me->SavedPhoneNumbers, file);
  file.close();

  return SIM800_RES_OK;
}

/**
 * @brief 
 * 
 * @param PhoneNum 
 * @return String 
 */
static sim800_res_t fNormalizedPhoneNumber(String PhoneNumber, String *Normalized) {

  if(PhoneNumber.length() == 11 && PhoneNumber.startsWith("0")) {
    *Normalized = PhoneNumber;
  } 
  else if(PhoneNumber.length() == 13 && PhoneNumber.startsWith("+")) {
    *Normalized = "0" + PhoneNumber.substring(3);
  } else {
    return SIM800_RES_PHONENUMBER_INVALID;
  }

  return SIM800_RES_OK;
}

/**
 * @brief 
 * 
 * @return sim800_res_t 
 */
static sim800_res_t fGSM_Init(sSim800 * const me) {

  if(fSim800_SendCommand(me, AT, ATOK) != SIM800_RES_OK) {
    // RestartGSM();
    return SIM800_RES_SEND_COMMAND_FAIL;
  }
  if(fSim800_SendCommand(me,CHECK_SIMCARD_INSERTED, SIMCARD_INSERTED) != SIM800_RES_OK) {
    return SIM800_RES_SIMCARD_NOT_INSERTED;
  }
  if(fSim800_SendCommand(me,RESET_FACTORY, ATOK) != SIM800_RES_OK) {
    return SIM800_RES_SEND_COMMAND_FAIL;
  }
  if(fSim800_SendCommand(me,IRANCELL, ATOK) != SIM800_RES_OK) {
    return SIM800_RES_SEND_COMMAND_FAIL;
  }
  if(fSim800_SendCommand(me,DELETE_ALL_MSGS, ATOK) != SIM800_RES_OK) {
    return SIM800_RES_SEND_COMMAND_FAIL;
  }
  if(fSim800_SendCommand(me,SET_TEXT_MODE, ATOK) != SIM800_RES_OK) {
    return SIM800_RES_SEND_COMMAND_FAIL;
  }
  if(fSim800_SendCommand(me,SET_TEXT_MODE, ATOK) != SIM800_RES_OK) {
    return SIM800_RES_SEND_COMMAND_FAIL;
  }
  if(fSim800_SendCommand(me,SET_TEXT_MODE_CONFIG, ATOK) != SIM800_RES_OK) {
    return SIM800_RES_SEND_COMMAND_FAIL;
  }
  if(me->EnableDeliveryReport) {
    if(fSim800_SendCommand(me, DELIVERY_ENABLE, ATOK) != SIM800_RES_OK) {
      return SIM800_RES_SEND_SMS_FAIL;
    }
  }

  return SIM800_RES_OK;
}

static sim800_res_t fRecivedSms_Parse(sSim800 * const me, const String *pLine) {

  if (!pLine->startsWith("+CMGL:")) {
    return SIM800_RES_REVIEVED_SMS_INVALID;
  }

  // Example: +CMGL: 1,"REC UNREAD","+989123456789","","25/09/12,21:32:15+14"
  int firstComma = pLine->indexOf(',');
  if (firstComma == -1) return SIM800_RES_REVIEVED_SMS_INVALID;

  // Extract index
  String idxStr = pLine->substring(6, firstComma);
  me->_args.MassageData.index = idxStr.toInt();

  // Extract phone number (inside 3rd quoted string)
  int firstQuote = pLine->indexOf('"', firstComma + 1);   // "REC UNREAD"
  int secondQuote = pLine->indexOf('"', firstQuote + 1);

  int thirdQuote = pLine->indexOf('"', secondQuote + 1);  // phone number
  int fourthQuote = pLine->indexOf('"', thirdQuote + 1);

  if (thirdQuote == -1 || fourthQuote == -1) return SIM800_RES_REVIEVED_SMS_INVALID;
  String phoneNumber = pLine->substring(thirdQuote + 1, fourthQuote);

  if(fNormalizedPhoneNumber(phoneNumber, &me->_args.MassageData.phoneNumber) != SIM800_RES_OK) {
    return SIM800_RES_PHONENUMBER_INVALID;
  }

  // Extract datetime (last quoted string)
  int lastQuoteOpen = pLine->lastIndexOf('"');
  int lastQuoteClose = pLine->lastIndexOf('"', lastQuoteOpen - 1);
  if (lastQuoteOpen > lastQuoteClose) {
    me->_args.MassageData.dateTime = pLine->substring(lastQuoteClose + 1, lastQuoteOpen);
  }

  return SIM800_RES_OK;
}

/**
 * @brief 
 * 
 * @param pRecSms 
 * @return sim800_res_t 
 */
static sim800_res_t fRecivedSms_CheckCommand(sSim800 * const me) {

  if(me->SavedPhoneNumbers.containsKey(me->_args.MassageData.phoneNumber)) {

    Serial.println("Recived sms from admin, check command");

    me->_args.MassageData.Massage.toLowerCase();
    bool commandIsValid = false;

    if(me->_args.MassageData.Massage.indexOf(SYSTEM) != -1) {

      me->_args.CommandType = eSYSTEM_COMMAND;
      commandIsValid = true;
    
    } else if(me->_args.MassageData.Massage.indexOf(LAMP) != -1) {

      me->_args.CommandType = eLAMP_COMMAND;
      commandIsValid = true;

    } else if(me->_args.MassageData.Massage.indexOf(SMSIP) != -1) {

      me->_args.CommandType = eIP_COMMAND;
      commandIsValid = true;

    } else if (me->_args.MassageData.Massage.indexOf(ALARM) != -1) {

      me->_args.CommandType = eALARM_COMMAND;
      commandIsValid = true;

    } else if (me->_args.MassageData.Massage.indexOf(MONOXIDE)!=-1) {

      me->_args.CommandType = eMONIXIDE_COMMAND;
      commandIsValid = true;

    } else if(me->_args.MassageData.Massage.indexOf(FIRE)!=-1) {

      me->_args.CommandType = eFIRE_COMMAND;
      commandIsValid = true;

    } else if(me->_args.MassageData.Massage.indexOf(HUMIDITY)!=-1) {
      
      me->_args.CommandType = eHUMIDITY_COMMAND;
      commandIsValid = true;
    } else if (me->_args.MassageData.Massage.indexOf(TEMP)!=-1) {

      me->_args.CommandType = eTEMP_COMMAND;
      commandIsValid = true;
    }
    
    if(commandIsValid) {

      fNotifyEventCommand_();
      return SIM800_RES_OK;

    } else {
      return SIM800_RES_REVIEVED_SMS_INVALID;
    }

  } else {
    return SIM800_RES_PHONENUMBER_INVALID;
  }

  return SIM800_RES_OK;
}

/**End of Group_Name
  * @}
  */
/************************ © COPYRIGHT DideGroup *****END OF FILE****/