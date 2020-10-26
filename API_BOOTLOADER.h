/**
  ************************************************************************************
  * @file    API_BOOTLOADER.h
  * @author  Sarp Engin DALTABAN
  * @version V1.0.0
  * @date    11-October-2019
  * @brief   Remote Bootloader&IOT functions header file
  ************************************************************************************
  */

#ifndef __API_BOOTLOADER_H__
#define __API_BOOTLOADER_H__

/* Includes ------------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"
#include "string.h"
#include "stdbool.h"
#include "stdlib.h"
#include "stdio.h"
#include "API_IWDG.h"
#include "main.h"
#include "usart.h"
#include "StringLib.h"
#include "API_USART.h"

/***************************  Flash Configuration Definitions ***********************/
#define APPLICATION_ADDRESS 																0x08080000
#define STORAGE_ADDRESS     																0x080C0000
#define MAX_APPICATION_SIZE																	262144																			/*bytes*/
#define GSM_UART  																					huart6
#define WIFI_UART 																					huart3

/************************** Built in bootloader SRAM trigger ************************/
#define CONTROL_VALUE_SRAM_ADDRESS		(uint32_t)0x20003FF0U
#define CONTROL_VALUE	(uint32_t)								0x626F6F74U

/***************************  Bootloader Definitions ********************************/
#define PERIODIC_FW_UPDATE_TIME       (16 * 60 * 60 * 1000) /* ms */
#define CURRENT_FW_VER          			"0.0.0"
#define BOOTLOADER_MODE         			0											/*To prepare an update, set this definition to '1'.
																														Additionally, don't forget to make the IROM1 address equal to APPLICATION_ADDRESS at target options of Keil.
																														This definition decides to call bootloader function at systemInit() and, 
																														redefines vector table offset at system_stm32f7xx.c:
																														...
																														#if BOOTLOADER_MODE
																															#define VECT_TAB_OFFSET   APPLICATION_ADDRESS
																														#else
																															#define VECT_TAB_OFFSET   0x00																		 
																														#endif
																														...
																														void SystemInit(void)
																														{
																															#if !BOOTLOADER_MODE
																																vBootloader();
																															#endif
																															...
																														}
																														*/

/****************** QUECTEL UG95 GSM Configuration Definitions **********************/
#define GSM_BUFFER																					gsm.receive																	/*Global GSM buffer*/
#define GSM_BUFFER_RECEIVE_INDEX														gsm.rx_index 																/*GSM Buffer's global index*/
#define clearGSMBufferAndResetItsIndex(x)   								gsmquectel_clearAllParams(x)								/*Clear the global GSM buffer and reset its index*/
#define GSM_TIME_PASSED_FROM_LAST_BYTE_RECEIVED							gsm.timeOut    															/*"ms" time passed after last byte received for gsm*/
#define GSM_EXTERNAL_IP																			gsmParams.ipAddress													/*IP buffer of gsm*/
#define GSM_TCP_SOCKET_CONTEXT_ID														1																						/*For web server connection, context ID*/
#define GSM_TCP_SOCKET_CONNECT_ID														0																						/*For web server connection, socket number*/
#define GSM_UDP_SOCKET_CONTEXT_ID														1																						/*For UDP server connection, context ID*/
#define GSM_UDP_SOCKET_CONNECT_ID														2																						/*For UDP server connection, socket number*/
#define GSM_MODULE_STATE																		gsmState																		/*Current State Of GSM*/
#define GSM_STEADY_STATE																		GSM_FINAL_STATE															/*If GSM state is steady, ready to communicate*/

/********************* ESP8266 WIFI Configuration Definitions ***********************/
#define clearWifiBufferAndResetItsIndex(x)									wifi_clearParams(x)													/*Clear the global WIFI buffer and reset its index*/
#define WIFI_TCP_SOCKET_NO																	4																						/*For web server connection, socket number*/
#define WIFI_UDP_SOCKET_NO																	3																						/*For UDP server connection, socket number*/
#define WIFI_BUFFER																					wifiParams.receiveBuffer										/*Global WIFI buffer*/
#define WIFI_EXTERNAL_IP																		wifiParams.externalIP												/*Wifi IP char buffer*/
#define WIFI_UART_RECEIVED_CHARACTER  											wifiParams.receivedData											/*Char, for baudrate switch triggering, make this global and known*/
#define WIFI_STATE																					wifiPreviousState														/*Current State Of wifi*/
#define WIFI_STEADY_STATE																		PROCESS_SUCCESS															/*If wifi state is steady, ready to communicate*/

/***************************** WEB Serer Definitions ********************************/
#define FIRMWARE_VERSION_WEB_SERVER_ADDRESS									"\"home.inavitas.io\""											/*To ask if server contains a new firmware*/
#define FIRMWARE_VERSION_WEB_SERVER_PORT										5555
#define FIRMWARE_VERSION_WEB_SERVER_PATH_FIRST_PART					"GET /api/Installer/checkFirmware?version="
#define FIRMWARE_VERSION_WEB_SERVER_PATH_SECOND_PART        " HTTP/1.1\r\nHost: home.inavitas.io:5555\r\ncache-control: no-cache\r\n\r\n"

/***************************** WATCHDOG RESET Definitions ***************************/
#define WATCHDOG_RESET(x)																		vIWDGReset(x)

/***************************** SAVING ENERGY REGISTERS Definitions ******************/
#define SAVE_ENERGY_REGISTERS(x)														vFlashSaveEnergyRegisters(x)

/***************************  To Activate Printf Debugs *****************************/
#define TFTP_BOOTLOADER_DEBUG																1

/*************************** Typedef Definitions ************************************/
typedef struct{
	
	bool triggerUpdateAtStartWifi, triggerUpdateAtStartGSM;
	bool wifiBootloading, gsmBootloading;
	bool changeTaskPriority;
	bool startTFTPTimeout;
	bool startGSMTimeout;
	
	char previousTftpBuffer[516], currentTftpBuffer[516];
	char remoteFixedPort[20];
	char newVersionNumber[5];
	char oldVersionNumber[5];
	char fileName[50];
	char remoteIP[20];
	
	uint8_t solvePort;
	uint8_t ACK[4];
	
	uint32_t askForUpdateCounter;
	uint32_t TFTPTimeoutCounter, connectionCounter;
	uint32_t incomingBlockNumber, incomingBlockNumberOld; 
	uint32_t checkSumCalculated, checkSumOnTheLastTFTPPackage;
	uint32_t applicationStoredAddressStart, applicationStoredAddressEnd;
	
	int remotePort;
	
} bootloaderVariables_t;

/************************* Extern Typedefs ******************************************/
extern bootloaderVariables_t  xBootloaderVariables;

/************************ Bootloader Function Prototypes ****************************/
void vBootloader(void);
void vEraseStorageSpace(void);
void vBuiltInBootloader(void);
void vBootloaderWifiEngage(void);
void vEraseApplicationSpace(void);
void vBootloadercallOver1ms(void);
void vBootloaderProcessTimers(void);
void vBootloaderQuectelEngage(void);
void vBootloadervariablesInit(void);
void vTFTPIncrementACK(uint8_t ACK[]);
void vReduceWifiBaudRateTo19200(void);
void vBootloaderJumpToApplication(uint32_t appSpace);
void vAskFirmwareVersionRequestGSM(void);
void vFlashEraseSector(uint32_t sectorNo);
void vAskFirmwareVersionRequestWifi(void);
void vFlashChecksumAndFirmwareVersion(void);
void vCopyStorageSpaceToApplicationSpace(uint32_t appSpace, uint32_t storageSpace, uint32_t spaceSize);
bool bIsTheLastTFTPPackage(uint32_t packageLength);
void vTFTPSendAcknowledge(char ack[], uint32_t size);
void vBootloaderCRC32ToFlash(uint32_t tftpBufferIndex);
void vGetDeviceFirmwareVersion(char firmwareVersion[]);
uint32_t crc32(const void *buf, size_t size, uint32_t init);
void vFlashTFTPBuffer(char tftpBuffer[], uint32_t bufferIndex);
void vEvaluateCRC32(uint32_t crcCalculated, uint32_t crcGiven);
void vPrintTFTPBlockNumber(uint32_t blockNumber, bool correctOrIncorrect);
void vTFTPReadRequestWifi(char remoteIP[], char remoteFixedPort[], char fileName[]);
void vPrepareTFTPReadRequest(char readRequest[], char fileName[], uint32_t *length);
void vTFTPReadRequestQuectel(char remoteIP[], char remoteFixedPort[], char fileName[]);
void vCalculateCyclicCRC32(uint32_t *calculatedCRC32, char tftpBuffer[], uint32_t size);
bool bCheckIfResponseReceivedOnTime(char expectedResponse[], char inputBuffer[], uint32_t timeout);
void vExtractCRCFromTheLastTFTPPackage(uint32_t *checsumExtracted, char tftpBuffer[], uint32_t tftpBufferIndex, uint32_t *crcBufferIndex);

#endif /* __API_BOOTLOADER_H_ */
