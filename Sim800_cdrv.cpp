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
#include <vector>

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
static sim800_res_t fRecivedSms_Parse(sSim800 * const me, const String line);
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
  if (me == NULL || !me->Init || me->IsSending) return;

  Serial.println("\n\nChecking inbox...");

  me->_args.CommandType = eNO_COMMAND;

  // Clear serial buffer
  while (me->ComPort->available()) me->ComPort->read();

  me->IsSending = true;
  me->ComPort->println("AT+CMGL=\"REC UNREAD\",1");
  me->ComPort->flush();

  unsigned long startTime = millis();
  bool finished = false;
  String lastLine;
  bool expectMessageBody = false;

  std::vector<int> messageIndices; // Store indices to delete later
  struct MessageData {
    int index;
    String phoneNumber;
    String message;
  };
  std::vector<MessageData> messages; // Store parsed messages

  // Step 1: Read and parse all messages
  while (!finished && (millis() - startTime < WAIT_FOR_COMMAND_RESPONSE_MS)) {
    if (me->ComPort->available() > 0) {
      String line = me->ComPort->readStringUntil('\n');
      line.trim();
      if (line.length() == 0) {
        if (expectMessageBody) continue;
        continue;
      }

      Serial.println("Received response line: " + line);

      if (line == "OK") {
        finished = true;
        break;
      }

      if (line == "ERROR") {
        Serial.println("Error response from SIM800");
        me->IsSending = false;
        return;
      }

      if (line.startsWith("+CMTI:")) {
        Serial.println("New SMS notification: " + line);
        continue;
      }

      if (line.startsWith("+CMGL:")) {
        Serial.println("-------Parse line-----------");
        Serial.println(line);
        if (fRecivedSms_Parse(me, line) != SIM800_RES_OK) {
          Serial.println("Failed to parse SMS header for index " + String(me->_args.MassageData.index));
          continue;
        }
        Serial.println("------------------");
        messageIndices.push_back(me->_args.MassageData.index);
        expectMessageBody = true;
        continue;
      }

      if (expectMessageBody) {
        me->_args.MassageData.Massage = line;
        expectMessageBody = false;

        // Store message data
        MessageData msg;
        msg.index = me->_args.MassageData.index;
        msg.phoneNumber = me->_args.MassageData.phoneNumber;
        msg.message = me->_args.MassageData.Massage;
        messages.push_back(msg);

        Serial.printf("SMS (index %d) from %s : %s\n",
                      msg.index, msg.phoneNumber.c_str(), msg.message.c_str());
      }

      lastLine = line;
    }
  }

  if (!finished) {
    Serial.println("Timeout or incomplete response. Last line: " + lastLine);
  }

  // Step 2: Process commands for all messages
  for (const auto& msg : messages) {
    me->_args.MassageData.index = msg.index;
    me->_args.MassageData.phoneNumber = msg.phoneNumber;
    me->_args.MassageData.Massage = msg.message;

    sim800_res_t cmdResult = fRecivedSms_CheckCommand(me);
    if (cmdResult != SIM800_RES_OK) {
      Serial.println("Command check failed for index " + String(msg.index) + ": " + String(cmdResult));
    } else if (me->_args.CommandType != eNO_COMMAND) {
      fNotifyEventCommand_();
    }
  }

  // Step 3: Delete all messages
  for (int index : messageIndices) {
    String deleteCmd = "AT+CMGD=" + String(index);
    if (fSim800_SendCommand(me, deleteCmd, "OK") != SIM800_RES_OK) {
      Serial.println("Failed to delete SMS index " + String(index));
    } else {
      Serial.println("Deleted SMS index " + String(index));
    }
    yield(); // Prevent watchdog timeout
  }

  Serial.printf("Processed %d messages\n", messages.size());
  me->IsSending = false;
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
sim800_res_t fSim800_SMSSend(sSim800 * const me, String PhoneNumber, String message) {

  me->IsSending = true;
  if(fSim800_SendCommand(me,SET_TEXT_MODE, ATOK) != SIM800_RES_OK) {
    return SIM800_RES_SEND_COMMAND_FAIL;
  }

  me->IsSending = true;
  String NormalizedPhoneNum;
  if(fNormalizedPhoneNumber(PhoneNumber, &NormalizedPhoneNum) != SIM800_RES_OK) {
    return SIM800_RES_PHONENUMBER_INVALID;
  }

  String TargetPhoneNumber = String(SET_PHONE_NUM) + "+98" + NormalizedPhoneNum.substring(1) + "\"";
  if(fSim800_SendCommand(me, TargetPhoneNumber, SEND_SMS_START)) {
    return SIM800_RES_SEND_COMMAND_FAIL;
  }

  me->IsSending = true;
  me->ComPort->print(message);
  me->ComPort->write(SEND_SMS_END);

  delay(10);

  if(fSim800_SendCommand(me, AT, ATOK)) {
    return SIM800_RES_SEND_COMMAND_FAIL;
  }

  me->IsSending = false;

	return SIM800_RES_OK;
}

/**
 * @brief 
 * 
 * @param me 
 * @param Text 
 * @return sim800_res_t 
 */
sim800_res_t fSim800_SMSSendToAll(sSim800 * const me, String message) {

  if(me == NULL) {

    Serial.println("SIM800 not initialized");
    return SIM800_RES_INIT_FAIL;
  }

  if (me->SavedPhoneNumbers.size() == 0) {

    Serial.println("No phone numbers in SavedPhoneNumbers");
    return SIM800_RES_PHONENUMBER_NOT_FOUND;
  }

  bool allSent = true;

  JsonObject phoneNumbers = me->SavedPhoneNumbers.as<JsonObject>();
  for (JsonObject::iterator it = phoneNumbers.begin(); it != phoneNumbers.end(); ++it) {

    String phoneNumber = it->key().c_str(); // Get phone number (key)
    int isAdmin = it->value().as<int>();    // Get isadmin value (0 or 1)

    Serial.printf("Sending SMS to %s (Admin: %d): %s\n", phoneNumber.c_str(), isAdmin, message.c_str());

    sim800_res_t result = fSim800_SMSSend(me, phoneNumber, message);
    if (result != SIM800_RES_OK) {

      Serial.printf("Failed to send SMS to %s: %d\n", phoneNumber.c_str(), result);
      allSent = false;

    } else {
      Serial.printf("Successfully sent SMS to %s\n", phoneNumber.c_str());
    }

    // Small delay to prevent overwhelming SIM800 and avoid brownout
    delay(1000); // Adjust based on SIM800 response time and power stability
    yield();     // Prevent watchdog timeout
  }

  return allSent ? SIM800_RES_OK : SIM800_RES_SEND_SMS_FAIL;
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
  
  if(me == NULL) return SIM800_RES_INIT_FAIL;

  // Clear serial buffer
  while (me->ComPort->available()) me->ComPort->read();

  me->IsSending = true;
  bool commandResponsed = false;
  int commandTries = 0;

  while(!commandResponsed && commandTries < me->CommandSendRetries) {

    commandTries++;
    Serial.printf("Sending %s (attempt %d/%d) -- desired response: %s\n",
                  Command.c_str(), commandTries, me->CommandSendRetries, DesiredResponse.c_str());

    me->ComPort->println(Command);
    me->ComPort->flush();
    unsigned long startTime = millis();

    String response;
    bool seenCommandEcho = false;
    while (!commandResponsed && (millis() - startTime < WAIT_FOR_COMMAND_RESPONSE_MS)) {
      if (me->ComPort->available() > 0) {
        String line = me->ComPort->readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        Serial.println("Response: " + line);

        // Skip command echo
        if (!seenCommandEcho && line == Command) {
          seenCommandEcho = true;
          continue;
        }

        response += line + "\n";

        if (line.indexOf(DesiredResponse) != -1) {
          Serial.printf("Command %s: Success\n", Command.c_str());
          commandResponsed = true;
          break;
        }
        if (line.indexOf("ERROR") != -1) {
          Serial.printf("Command %s: Failed with ERROR\n", Command.c_str());
          break;
        }
      }
      yield();
    }

    if (!commandResponsed) {
      Serial.println("No response for command " + Command + ", retrying...");
      while (me->ComPort->available()) me->ComPort->read(); // Clear buffer before retry
    }
  }

  me->IsSending = false;
  return commandResponsed ? SIM800_RES_OK : SIM800_RES_SEND_COMMAND_FAIL;
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
  // if(fSim800_SendCommand(me,DELETE_ALL_MSGS, ATOK) != SIM800_RES_OK) {
  //   return SIM800_RES_SEND_COMMAND_FAIL;
  // }
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

static sim800_res_t fRecivedSms_Parse(sSim800 * const me, const String line) {

  if (!line.startsWith("+CMGL:")) {
    return SIM800_RES_REVIEVED_SMS_INVALID;
  }

  // Example: +CMGL: 1,"REC UNREAD","+989123456789","","25/09/12,21:32:15+14"
  int firstComma = line.indexOf(',');
  if (firstComma == -1) return SIM800_RES_REVIEVED_SMS_INVALID;

  // Extract index
  String idxStr = line.substring(6, firstComma);
  me->_args.MassageData.index = idxStr.toInt();

  // Extract phone number (inside 3rd quoted string)
  int firstQuote = line.indexOf('"', firstComma + 1);   // "REC UNREAD"
  int secondQuote = line.indexOf('"', firstQuote + 1);

  int thirdQuote = line.indexOf('"', secondQuote + 1);  // phone number
  int fourthQuote = line.indexOf('"', thirdQuote + 1);

  if (thirdQuote == -1 || fourthQuote == -1) return SIM800_RES_REVIEVED_SMS_INVALID;
  String phoneNumber = line.substring(thirdQuote + 1, fourthQuote);

  if(fNormalizedPhoneNumber(phoneNumber, &me->_args.MassageData.phoneNumber) != SIM800_RES_OK) {
    return SIM800_RES_PHONENUMBER_INVALID;
  }

  // Extract datetime (last quoted string)
  int lastQuoteOpen = line.lastIndexOf('"');
  int lastQuoteClose = line.lastIndexOf('"', lastQuoteOpen - 1);
  if (lastQuoteOpen > lastQuoteClose) {
    me->_args.MassageData.dateTime = line.substring(lastQuoteClose + 1, lastQuoteOpen);
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
    me->_args.CommandType = eNO_COMMAND;

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
    
    // if(commandIsValid) {

    //   fNotifyEventCommand_();
    //   return SIM800_RES_OK;

    // } else {
    //   return SIM800_RES_REVIEVED_SMS_INVALID;
    // }
    if(commandIsValid) {
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