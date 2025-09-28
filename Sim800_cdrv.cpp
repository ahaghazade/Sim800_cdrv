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
	if(Sim800._pfCommandEvent != NULL) { \
		Sim800._pfCommandEvent(&(Sim800._args)); \
	}
/* Private typedef -----------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
const char* SavedPhoneNumbersPath = "/PhoneNumbers.json";

/* Private function prototypes -----------------------------------------------*/
static sim800_res_t fLoadPhoneNumbers(String Path);
static sim800_res_t fSavePhoneNumbers(String Path);
static sim800_res_t fNormalizedPhoneNumber(String PhoneNumber, String *Normalized);
static sim800_res_t fSendCommand(String Command, String DesiredResponse, String *pResponse = NULL);
static sim800_res_t fGSM_Init(void);
static sim800_res_t fInbox_Read(void);
static sim800_res_t fInbox_Clear(void);
static sim800_res_t fRecivedSms_Parse(const String *pLine);
static sim800_res_t fRecivedSms_CheckCommand(void);
static sim800_res_t fCheckForDeliveryReport(void);

/* Variables -----------------------------------------------------------------*/
sSim800 Sim800;

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
sim800_res_t fSim800_Init(void) {


  Sim800.Init = false;

  if (!SPIFFS.begin(true)) { 
    return SIM800_RES_INIT_FAIL;
  }

  if(fLoadPhoneNumbers(SavedPhoneNumbersPath) != SIM800_RES_OK) {
    return SIM800_RES_INIT_FAIL;
  }

  if(fGSM_Init() != SIM800_RES_OK) {
    return SIM800_RES_INIT_FAIL;
  }

  Sim800.EnableSengingSMS = true;
  Sim800.Init = true;
  Sim800.IsSending = false;

  return SIM800_RES_OK;
}

/**
 * @brief 
 * 
 * @param me 
 */
void fSim800_Run(void) {

  if(!Sim800.Init || Sim800.IsSending) return;

  Sim800.IsSending = true;
  Sim800.ComPort->println("AT+CMGL=\"REC UNREAD\"");

  unsigned long startTime = millis();
  eSmsState state = SMS_IDLE;
  int pendingDeleteIndex = -1;

  Serial.println("checking inbox...");

  while(millis() - startTime < WAIT_FOR_COMMAND_RESPONSE_MS) {

    if(Sim800.ComPort->available() > 0) {

      String line = Sim800.ComPort->readStringUntil('\n');
      line.trim();
      if(line.length() == 0) continue;

      if (line == "OK") {
        break; // end of listing
      }

      if(line.startsWith("+CMGL:")) {

        Serial.print("parsing line: ");Serial.println(line);

        if(fRecivedSms_Parse(&line) == SIM800_RES_OK) {
          state = SMS_BODY;//next lines are body
        }else {
          state = SMS_IDLE;
        }

      }else if(state == SMS_BODY) {

        Serial.println("----------New massage-----------");
        Serial.println(line);
        // This is SMS body
        Sim800._args.MassageData.Massage = line;

        Serial.printf("SMS (index %d) from %s : %s\n",
          Sim800._args.MassageData.index,
          Sim800._args.MassageData.phoneNumber,
          Sim800._args.MassageData.Massage.c_str()
        );

        // process SMS
        fRecivedSms_CheckCommand();

        // mark for deletion
        pendingDeleteIndex = Sim800._args.MassageData.index;

        state = SMS_IDLE;
      }
    }
  }

  // Delete after finishing loop
  if (pendingDeleteIndex >= 0) {

    Serial.printf("deleting massage index %d\n", pendingDeleteIndex);
    String deleteCmd = "AT+CMGD=" + String(pendingDeleteIndex) + ",0";
    fSendCommand(deleteCmd, "OK");
  }

  Sim800.IsSending = false;
}


/**
 * @brief 
 * 
 * @param me 
 * @param PhoneNumber 
 * @param IsAdmin 
 * @return sim800_res_t 
 */
sim800_res_t fSim800_AddPhoneNumber(String PhoneNumber, bool IsAdmin) {

  if(!Sim800.Init) {
    return SIM800_RES_INIT_FAIL;
  }

  String NormalizedPhoneNumber;
  if(fNormalizedPhoneNumber(PhoneNumber, &NormalizedPhoneNumber) != SIM800_RES_OK) {
    return SIM800_RES_PHONENUMBER_INVALID;
  }

  Sim800.SavedPhoneNumbers[NormalizedPhoneNumber] = IsAdmin? 1 : 0;
  fSavePhoneNumbers(SavedPhoneNumbersPath);

  return SIM800_RES_OK;
}

/**
 * @brief 
 * 
 * @param me 
 * @param PhoneNumber 
 * @return sim800_res_t 
 */
sim800_res_t fSim800_RemovePhoneNumber(String PhoneNumber) {

  if (!Sim800.Init) {
    return SIM800_RES_INIT_FAIL;
  }

  String NormalizedPhoneNumber;
  if (fNormalizedPhoneNumber(PhoneNumber, &NormalizedPhoneNumber) != SIM800_RES_OK) {
    return SIM800_RES_PHONENUMBER_INVALID;
  }

  if (!Sim800.SavedPhoneNumbers.containsKey(NormalizedPhoneNumber)) {
    return SIM800_RES_PHONENUMBER_NOT_FOUND;
  }

  Sim800.SavedPhoneNumbers.remove(NormalizedPhoneNumber);
  fSavePhoneNumbers(SavedPhoneNumbersPath);

  return SIM800_RES_OK;
}

/**
 * @brief 
 * 
 * @param me 
 * @param PhoneNumber 
 * @return sim800_res_t 
 */
sim800_res_t fSim800_RemoveAllPhoneNumbers(void) {

  if(!Sim800.Init) {
    return SIM800_RES_INIT_FAIL;
  }

  Sim800.SavedPhoneNumbers.clear();

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
sim800_res_t fSim800_SMSSend(String PhoneNumber, String Text) {

  int retryCount = 0;
  bool deliveryReceived = false;

  Serial.print("Sending sms to ");Serial.println(PhoneNumber);

  if(fSendCommand(SET_TEXT_MODE, ATOK) != SIM800_RES_OK) {
    Sim800.IsSending = false;
    return SIM800_RES_SEND_COMMAND_FAIL;
  }

  if(Sim800.EnableDeliveryReport) {
    if(fSendCommand( DELIVERY_ENABLE, ATOK) != SIM800_RES_OK) {
      Sim800.IsSending = false;
      return SIM800_RES_SEND_SMS_FAIL;
    }
  }

  String NormalizedPhoneNum;
  if(fNormalizedPhoneNumber(PhoneNumber, &NormalizedPhoneNum) != SIM800_RES_OK) {
    Sim800.IsSending = false;
    return SIM800_RES_PHONENUMBER_INVALID;
  }

  while(retryCount < WAIT_FOR_SIM800_SEND_SMS_TRYES && !deliveryReceived) {

    unsigned long startTime = millis();
    while(Sim800.IsSending && (millis() - startTime < WAIT_FOR_SIM800_READY_SEND_COMMAND)){};

    if(Sim800.IsSending) {
      return SIM800_RES_SEND_SMS_FAIL;
    }

    Serial.println("Start sending");

    String TargetPhoneNumber = String(SET_PHONE_NUM) + "+98" + NormalizedPhoneNum.substring(1) + "\"";
    if(fSendCommand(TargetPhoneNumber, SEND_SMS_START)) {
      Sim800.IsSending = false;
      return SIM800_RES_SEND_COMMAND_FAIL;
    }

    Sim800.IsSending = true;
    Sim800.ComPort->print(Text);
    Sim800.ComPort->write(SEND_SMS_END);
    delay(100);
    Sim800.IsSending = false;
    
    if(Sim800.EnableDeliveryReport) {

      // Check for delivery report if enabled
      if(Sim800.EnableDeliveryReport) {

        if(fCheckForDeliveryReport() == SIM800_RES_OK) {

          Serial.println("SMS delivery confirmed.");
          deliveryReceived = true;
          break;
        } else {
          Serial.println("No delivery report received within timeout.");
        }
      } else {
        deliveryReceived = true; // No delivery report check, assume success
        break;
      }
    }
  }

    Sim800.IsSending = false;

    if (!deliveryReceived && Sim800.EnableDeliveryReport) {

    Serial.println("All SMS retries failed. Initiating call to " + PhoneNumber);
    String phonenumber_call = "+98" + PhoneNumber.substring(1); // Adjust phone number format
    String phonenumber_str = "ATD" + phonenumber_call + ";";
    Sim800.ComPort->println(phonenumber_str.c_str());

    // Wait for call response (e.g., OK or ERROR)
    unsigned long callStartTime = millis();
    bool callSuccess = false;

    while (millis() - callStartTime < WIAT_FOR_CALL_RESPONSE) { // 5-second timeout for call response

      if (Sim800.ComPort->available()) {

        String response = Sim800.ComPort->readString();
        if (response.indexOf("OK") != -1) {
          Serial.println("Call initiated successfully.");
          callSuccess = true;
          break;
        } else if (response.indexOf("ERROR") != -1) {
          Serial.println("Call failed: ERROR response received.");
          break;
        }
      }
    }

    if (!callSuccess) {
      Serial.println("Call failed: No valid response within timeout.");
      return SIM800_RES_CALL_FAIL; // Define this in your enum
    }
    return SIM800_RES_DELIVERY_REPORT_FAIL; // SMS failed, call attempted
  }

  Sim800.IsSending = false;
  return deliveryReceived ? SIM800_RES_OK : SIM800_RES_DELIVERY_REPORT_FAIL;
}

/**
 * @brief 
 * 
 * @return sim800_res_t 
 */
static sim800_res_t fCheckForDeliveryReport(void) {

  unsigned long startTime = millis();

  while (millis() - startTime < WAIT_FOR_SIM800_SEND_SMS_DELIVERY) {
    
    if(Sim800.ComPort->available()) {

      String incomingData = Sim800.ComPort->readString();
      if (incomingData.indexOf("+CDS:") != -1) {
        Serial.println("delivery report reviceved");
        return SIM800_RES_OK;
      }
    }
  }
  return SIM800_RES_DELIVERY_REPORT_FAIL;
}

/**
 * @brief 
 * 
 * @param me 
 * @param message 
 * @return sim800_res_t 
 */
sim800_res_t fSim800_SMSSendToAll(String message) {


  if (Sim800.SavedPhoneNumbers.size() == 0) {

    Serial.println("No phone numbers in SavedPhoneNumbers");
    return SIM800_RES_PHONENUMBER_NOT_FOUND;
  }

  bool allSent = true;

  JsonObject phoneNumbers = Sim800.SavedPhoneNumbers.as<JsonObject>();
  for (JsonObject::iterator it = phoneNumbers.begin(); it != phoneNumbers.end(); ++it) {

    String phoneNumber = it->key().c_str(); // Get phone number (key)
    int isAdmin = it->value().as<int>();    // Get isadmin value (0 or 1)

    Serial.printf("Sending SMS to %s (Admin: %d): %s\n", phoneNumber.c_str(), isAdmin, message.c_str());

    sim800_res_t result = fSim800_SMSSend(phoneNumber, message);
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
 * @param pBalance 
 * @return sim800_res_t 
 */
sim800_res_t fSim800_GetSimcardBalance(uint16_t *pBalance) {


  return SIM800_RES_OK;
}

/**
 * @brief 
 * 
 * @return uint32_t 
 */
uint32_t fSim800_CheckCredit(void) {

  fSendCommand("AT+CUSD=1", ATOK); 
  fSendCommand("AT+CUSD=1,\"*555*4*3#\"", "72");
  fSendCommand("AT+CUSD=1,\"2\"", "English"); 
  //esp_task_wdt_reset();
  delay(2000);
  //esp_task_wdt_reset();
  String balanceLine;
  fSendCommand("AT+CUSD=1,\"*555*1*2#\"","Credit:", &balanceLine); 
  fSendCommand("AT+CUSD=0",ATOK); 
  //"CUSD: 0, \"On 1403/07/01.your balance is 687348 RialsA new generation of MyIrancell super app *45#\", 15␍";
  int startIndex = balanceLine.indexOf("Credit:") + 7;
  int endIndex   = balanceLine.indexOf("IRR");
  String balanceStr = balanceLine.substring(startIndex, endIndex);
  balanceStr.replace(",", "");
  long balanceValue = balanceStr.toInt() / 10; //
  Serial.printf(">>>>>>>>  balance is: %d\n", balanceValue);
  return balanceValue;
}

/**
 * @brief 
 * 
 * @return JsonDocument 
 */
sim800_res_t fSim800_GetPhoneNumbers(JsonDocument *pDoc) {

  pDoc = &(Sim800.SavedPhoneNumbers);
  return SIM800_RES_OK;
}

/**
 * @brief 
 * 
 * @param me 
 * @param fpFunc 
 * @return uint8_t 
 */
sim800_res_t fSim800_RegisterCommandEvent(void(*fpFunc)(sSim800RecievedMassgeDone *pArgs)) {
	
	if(fpFunc == NULL) {
    return SIM800_RES_INIT_FAIL;
  }

	Sim800._pfCommandEvent = fpFunc;
	
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
static sim800_res_t fSavePhoneNumbers(String Path) {

  File file = SPIFFS.open(Path, FILE_WRITE);
  if(!file) {
    return SIM800_RES_LOAD_JSON_FIAL;
  }

  serializeJson(Sim800.SavedPhoneNumbers, file);
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
static sim800_res_t fLoadPhoneNumbers(String Path) {

  File file = SPIFFS.open(Path, FILE_READ);
  if(!file) {
    return SIM800_RES_LOAD_JSON_FIAL;
  }
  deserializeJson(Sim800.SavedPhoneNumbers, file);
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
 * @param me 
 * @param Command 
 * @param DesiredResponse 
 * @return sim800_res_t 
 */
static sim800_res_t fSendCommand(String Command, String DesiredResponse, String *pResponse) {

  bool commandResponsed = false;
  int commandTries = 0;

  unsigned long startTime = millis();
  while(Sim800.IsSending && millis() - startTime < WAIT_FOR_SIM800_READY_SEND_COMMAND){};
  if(Sim800.IsSending) {
    return SIM800_RES_SEND_COMMAND_FAIL;
  }

  while(!commandResponsed && commandTries < Sim800.CommandSendRetries) {
    
    commandTries++;
    Sim800.IsSending = true;
    
    Serial.printf("\nSending %s  ...(%d) -- desired response: %s\n", Command.c_str(), commandTries, DesiredResponse);

    Sim800.ComPort->println(Command);
    unsigned long startTime = millis();

    while(!commandResponsed && millis() - startTime < WAIT_FOR_COMMAND_RESPONSE_MS) {

      while(Sim800.ComPort->available() > 0) {  

        String line = Sim800.ComPort->readString();

        Serial.println("==== readed data =====");  
        Serial.println(line);
        Serial.print("line indexOf(DesiredRes): ");
        Serial.println(line.indexOf(DesiredResponse));

        if (line.indexOf(DesiredResponse) != -1) {

          Serial.printf("Request %s : Success.\n", Command.c_str());
          if(pResponse != NULL) {
            *pResponse = line;
          }
          commandResponsed = true;
          break;
        }
      }
    }
    delay(100);
  }

  Sim800.IsSending = false;

  if(commandResponsed == true) {
    return SIM800_RES_OK;
  } else {
	return SIM800_RES_SEND_COMMAND_FAIL;
  }
}

/**
 * @brief 
 * 
 * @return sim800_res_t 
 */
static sim800_res_t fGSM_Init(void) {

  if(fSendCommand(AT, ATOK) != SIM800_RES_OK) {
    Sim800.IsSending = false;
    // RestartGSM();
    return SIM800_RES_SEND_COMMAND_FAIL;
  }
  if(fSendCommand(CHECK_SIMCARD_INSERTED, SIMCARD_INSERTED) != SIM800_RES_OK) {
    Sim800.IsSending = false;
    return SIM800_RES_SIMCARD_NOT_INSERTED;
  }
  if(fSendCommand(RESET_FACTORY, ATOK) != SIM800_RES_OK) {
    Sim800.IsSending = false;
    return SIM800_RES_SEND_COMMAND_FAIL;
  }
  if(fSendCommand(IRANCELL, ATOK) != SIM800_RES_OK) {
    Sim800.IsSending = false;
    return SIM800_RES_SEND_COMMAND_FAIL;
  }
  if(fSendCommand(DELETE_ALL_MSGS, ATOK) != SIM800_RES_OK) {
    Sim800.IsSending = false;
    return SIM800_RES_SEND_COMMAND_FAIL;
  }
  if(fSendCommand(SET_TEXT_MODE, ATOK) != SIM800_RES_OK) {
    Sim800.IsSending = false;
    return SIM800_RES_SEND_COMMAND_FAIL;
  }
  if(fSendCommand(SET_TEXT_MODE_CONFIG, ATOK) != SIM800_RES_OK) {
    Sim800.IsSending = false;
    return SIM800_RES_SEND_COMMAND_FAIL;
  }
  if(Sim800.EnableDeliveryReport) {
    if(fSendCommand( DELIVERY_ENABLE, ATOK) != SIM800_RES_OK) {
      Sim800.IsSending = false;
      return SIM800_RES_SEND_SMS_FAIL;
    }
  }

  fSim800_CheckCredit();

  return SIM800_RES_OK;
}

static sim800_res_t fRecivedSms_Parse(const String *pLine) {

  if (!pLine->startsWith("+CMGL:")) {
    return SIM800_RES_REVIEVED_SMS_INVALID;
  }

  // Example: +CMGL: 1,"REC UNREAD","+989123456789","","25/09/12,21:32:15+14"
  int firstComma = pLine->indexOf(',');
  if (firstComma == -1) return SIM800_RES_REVIEVED_SMS_INVALID;

  // Extract index
  String idxStr = pLine->substring(6, firstComma);
  Sim800._args.MassageData.index = idxStr.toInt();

  // Extract phone number (inside 3rd quoted string)
  int firstQuote = pLine->indexOf('"', firstComma + 1);   // "REC UNREAD"
  int secondQuote = pLine->indexOf('"', firstQuote + 1);

  int thirdQuote = pLine->indexOf('"', secondQuote + 1);  // phone number
  int fourthQuote = pLine->indexOf('"', thirdQuote + 1);

  if (thirdQuote == -1 || fourthQuote == -1) return SIM800_RES_REVIEVED_SMS_INVALID;
  String phoneNumber = pLine->substring(thirdQuote + 1, fourthQuote);

  if(fNormalizedPhoneNumber(phoneNumber, &Sim800._args.MassageData.phoneNumber) != SIM800_RES_OK) {
    return SIM800_RES_PHONENUMBER_INVALID;
  }

  // Extract datetime (last quoted string)
  int lastQuoteOpen = pLine->lastIndexOf('"');
  int lastQuoteClose = pLine->lastIndexOf('"', lastQuoteOpen - 1);
  if (lastQuoteOpen > lastQuoteClose) {
    Sim800._args.MassageData.dateTime = pLine->substring(lastQuoteClose + 1, lastQuoteOpen);
  }

  return SIM800_RES_OK;
}

/**
 * @brief 
 * 
 * @param pRecSms 
 * @return sim800_res_t 
 */
static sim800_res_t fRecivedSms_CheckCommand(void) {

  if(Sim800.SavedPhoneNumbers.containsKey(Sim800._args.MassageData.phoneNumber)) {

    Serial.println("Recived sms from admin, check command");

    Sim800._args.MassageData.Massage.toLowerCase();
    bool commandIsValid = false;

    if(Sim800._args.MassageData.Massage.indexOf(SYSTEM) != -1) {

      Sim800._args.CommandType = eSYSTEM_COMMAND;
      commandIsValid = true;
    
    } else if(Sim800._args.MassageData.Massage.indexOf(LAMP) != -1) {

      Sim800._args.CommandType = eLAMP_COMMAND;
      commandIsValid = true;

    } else if(Sim800._args.MassageData.Massage.indexOf(SMSIP) != -1) {

      Sim800._args.CommandType = eIP_COMMAND;
      commandIsValid = true;

    } else if (Sim800._args.MassageData.Massage.indexOf(ALARM) != -1) {

      Sim800._args.CommandType = eALARM_COMMAND;
      commandIsValid = true;

    } else if (Sim800._args.MassageData.Massage.indexOf(MONOXIDE)!=-1) {

      Sim800._args.CommandType = eMONIXIDE_COMMAND;
      commandIsValid = true;

    } else if(Sim800._args.MassageData.Massage.indexOf(FIRE)!=-1) {

      Sim800._args.CommandType = eFIRE_COMMAND;
      commandIsValid = true;

    } else if(Sim800._args.MassageData.Massage.indexOf(HUMIDITY)!=-1) {
      
      Sim800._args.CommandType = eHUMIDITY_COMMAND;
      commandIsValid = true;
    } else if (Sim800._args.MassageData.Massage.indexOf(TEMP)!=-1) {

      Sim800._args.CommandType = eTEMP_COMMAND;
      commandIsValid = true;
    }
    
    if(commandIsValid) {

      Sim800.IsSending = false;
      fNotifyEventCommand_();
      Sim800.IsSending = true;
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