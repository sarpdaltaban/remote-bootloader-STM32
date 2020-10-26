/**
  ******************************************************************************
  * @file    API_BOOTLOADER.c
  * @author  Sarp Engin DALTABAN
  * @version V1.0.0
  * @date    11-October-2019
  * @brief   Remote Bootloader&IOT functions source file
  ******************************************************************************
  */
		
/* Includes ------------------------------------------------------------------*/
#include "API_BOOTLOADER.h"

/* Typedefs ------------------------------------------------------------------*/
bootloaderVariables_t  xBootloaderVariables;

/**
* @brief This function copies the data on the storage space to the application space if its checksum bit at the end of the space is 1,
*				 otherwise, jumps at application space if there is data on that space and its checksum bit at the end of the space is 1, showing firmware is verified
*				 else starts normally from the boot space.
* @note  This function should be called at very beginning of the program, such as, insert it as the first line of the function systemInit() as told in API_BOOTLOADER.h
*/
void vBootloader(void)
{
	HAL_FLASH_Unlock();
	
	if((int)*(__IO uint32_t *)STORAGE_ADDRESS != -1)																										/*If there is data on the start address of the storage space*/
	{
		if(*(__IO uint32_t *)(STORAGE_ADDRESS + MAX_APPICATION_SIZE - 4) == 1)														/*If checksum calculation bit is set*/
		{
			vEraseApplicationSpace();																																				/*Erase application space*/
			vCopyStorageSpaceToApplicationSpace(APPLICATION_ADDRESS, STORAGE_ADDRESS, MAX_APPICATION_SIZE);	/*Copy storage space to app space*/
			vEraseStorageSpace();																																						/*Erase the storage sector*/
			vBootloaderJumpToApplication(APPLICATION_ADDRESS);																							/*Jump to the application*/
		}
		else																																															/*If program stopped before checksum calculation, electric cut etc.*/
		{
			vEraseStorageSpace();																																						/*Erase the storage sector*/
			NVIC_SystemReset();																																							/*Reset the system*/
		}
	}
	else
	{
		if((int)*(__IO uint32_t *)APPLICATION_ADDRESS != -1)																							/*If there is data on the start address of the application space*/
		{
			if(*(__IO uint32_t*)(APPLICATION_ADDRESS + MAX_APPICATION_SIZE - 4) == 1)												/*If checksum calculation bit is set*/
			{	
				vBootloaderJumpToApplication(APPLICATION_ADDRESS);																						/*Jump to the approved application space*/
			}
			else																																														/*If program stopped before checksum calculation, electric cut etc.*/
			{																																		
				vEraseApplicationSpace();																																			/*Erase unapproved application space*/
				NVIC_SystemReset();																																						/*Reset the system*/
			}
		}
		else																																															/*If there is neither data on the Application space nor on the Storage space*/
		{
																																																			/*Do nothing, start working as a bootloader application*/
		}
	}
}

/**
* @brief This function makes device enter STM32 dfuse bootloader mode.
*/
void vBuiltInBootloader(void)
{
	if ( *((unsigned long *)CONTROL_VALUE_SRAM_ADDRESS) == CONTROL_VALUE)
	{
		void (*SysMemBootJump)(void);
		*((unsigned long *)CONTROL_VALUE_SRAM_ADDRESS) =  0x0; // Reset the trigger
		vEraseApplicationSpace();
		__set_MSP(*(__IO uint32_t*)0x1FFF0000);
		SysMemBootJump = (void (*)(void)) (*((uint32_t *) 0x1FFF0004)); // Point the PC to the System Memory reset vector (+4)
		SysMemBootJump();
		while (1);
	}
}

/**
* @brief This function sends device firmware version to a web server using WIFI. 
*				 Web server returns a file name if the device firmware version is NOT the most recent.
*/
void vAskFirmwareVersionRequestWifi(void)
{
	if (WIFI_EXTERNAL_IP[0] != 0 && WIFI_STATE == WIFI_STEADY_STATE)
	{
		if (xBootloaderVariables.triggerUpdateAtStartWifi || xBootloaderVariables.askForUpdateCounter >= PERIODIC_FW_UPDATE_TIME)
		{
			/*Update should be requested at device start or because of the periodic update timer*/
			if (xBootloaderVariables.triggerUpdateAtStartWifi)
			{
				xBootloaderVariables.triggerUpdateAtStartWifi = false;
			} 
			else if(xBootloaderVariables.askForUpdateCounter >= PERIODIC_FW_UPDATE_TIME)/*global timer triggers bootloader request*/
			{
				xBootloaderVariables.askForUpdateCounter = 250000;
			}
			
			#if TFTP_BOOTLOADER_DEBUG
			printf("Wifi is asking for update..\r\n");
			#endif
			
			
			char remoteIP[20], remoteFixedPort[20], fileName[50], connectToTCPServer[100];
						
			HAL_FLASH_Unlock();
			
			clearWifiBufferAndResetItsIndex();
			
			sprintf(connectToTCPServer, "AT+CIPSTART=%i,\"TCP\",%s,%i\r\n", WIFI_TCP_SOCKET_NO, FIRMWARE_VERSION_WEB_SERVER_ADDRESS, FIRMWARE_VERSION_WEB_SERVER_PORT);
			
			/*connect to the TCP - Web server*/
			HAL_UART_Transmit_IT(&WIFI_UART, (uint8_t*)connectToTCPServer, strlen(connectToTCPServer));
						
			if (bCheckIfResponseReceivedOnTime("CONNECT\r\n\r\nOK\r\n", WIFI_BUFFER, 15000))/*if connected*/
			{
				char deviceVersionNumber[5], askFirmwareURLPath[150], sendQuantity[150], closeSocket[50];
				
				clearWifiBufferAndResetItsIndex();
				
				
				vGetDeviceFirmwareVersion(deviceVersionNumber);
				
								
				/*prepare the HTTP request to ask for update, send your version number to get if a new one*/
				sprintf(askFirmwareURLPath, "%s%s%s", FIRMWARE_VERSION_WEB_SERVER_PATH_FIRST_PART, deviceVersionNumber, FIRMWARE_VERSION_WEB_SERVER_PATH_SECOND_PART);
				
				sprintf(sendQuantity, "AT+CIPSEND=%i,%i\r\n", WIFI_TCP_SOCKET_NO, strlen(askFirmwareURLPath));
								
				
				HAL_UART_Transmit_IT(&WIFI_UART, (uint8_t*)sendQuantity, strlen(sendQuantity));
				bCheckIfResponseReceivedOnTime("> ", WIFI_BUFFER, 5000);
				clearWifiBufferAndResetItsIndex();
				
				
				HAL_UART_Transmit_IT(&WIFI_UART, (uint8_t*)askFirmwareURLPath, strlen(askFirmwareURLPath));
				bCheckIfResponseReceivedOnTime("}}", WIFI_BUFFER, 15000);
								
				
				/*parse the response*/
				vGetSubstringBetweenTwoStrings(WIFI_BUFFER, "\"ip\":\"", "\"",   remoteIP);
				vGetSubstringBetweenTwoStrings(WIFI_BUFFER, "\"port\":\"", "\"", remoteFixedPort);
				vGetSubstringBetweenTwoStrings(WIFI_BUFFER, "\"file\":\"", "\"", fileName);
								
				if (strlen(fileName) > 0)/*if a new firmware found*/
				{
					vGetSubstringBetweenTwoStrings(WIFI_BUFFER, "rx-", "bin", xBootloaderVariables.newVersionNumber);
				}
				
				
				sprintf(closeSocket, "AT+CIPCLOSE=%i\r\n", WIFI_TCP_SOCKET_NO);
				HAL_UART_Transmit_IT(&WIFI_UART, (uint8_t*)closeSocket, strlen(closeSocket));/*close the socket*/
				bCheckIfResponseReceivedOnTime("OK\r\n", WIFI_BUFFER, 750);
				clearWifiBufferAndResetItsIndex();
				
				
				#if TFTP_BOOTLOADER_DEBUG
				printf("Wifi update ask request completed..\r\n");
				#endif
				
				
				vTFTPReadRequestWifi(remoteIP, remoteFixedPort, fileName);
			}
			else
			{
				#if TFTP_BOOTLOADER_DEBUG
				printf("Wifi could not connect to Web server to ask for firmware version..\r\n");
				#endif
			}
		}
	}
}

/**
* @brief this function connects to the tftp server and sends a read request to the server over Wifi
* @params char remoteIP[] 			 -> TFTP Server IP
*					char remoteFixedPort[] -> TFTP Server Port
*					char fileName[]				 -> name of the new firmware
*/
void vTFTPReadRequestWifi(char remoteIP[], char remoteFixedPort[], char fileName[])
{
	if(remoteIP[0] != 0x00 	&& remoteFixedPort[0] != 0x00 && fileName[0] != 0x00) /*if there is a file to be requested*/
	{
		char connectToUDPServer[100];
		
		HAL_FLASH_Unlock();
		
		vEraseStorageSpace();
		
		vReduceWifiBaudRateTo19200();
		
		clearWifiBufferAndResetItsIndex();
		
		sprintf(connectToUDPServer, "AT+CIPSTART=%i,\"UDP\",\"%s\",%s,69,2\r\n", WIFI_UDP_SOCKET_NO, remoteIP, remoteFixedPort);
		
		/*connect to the tftp server*/ 
		HAL_UART_Transmit_IT(&WIFI_UART, (uint8_t*)connectToUDPServer, strlen(connectToUDPServer));
		
		if(bCheckIfResponseReceivedOnTime("CONNECT\r\n\r\nOK\r\n", WIFI_BUFFER, 15000))/*if connected to the tftp server*/
		{
			uint32_t length;
			char tftpReadRequest[50], sendQuantity[50];
			
			xBootloaderVariables.wifiBootloading = true;
			
			clearWifiBufferAndResetItsIndex();
			
			/*turn access point off*/
			HAL_UART_Transmit_IT(&WIFI_UART, (uint8_t *)"AT+CWMODE=1\r\n", 	strlen("AT+CWMODE=1\r\n"));
			bCheckIfResponseReceivedOnTime("OK\r\n", WIFI_BUFFER, 2500);
			clearWifiBufferAndResetItsIndex();
			
			vPrepareTFTPReadRequest(tftpReadRequest, fileName, &length);
			
			sprintf(sendQuantity, "AT+CIPSEND=%i,%i\r\n", WIFI_UDP_SOCKET_NO, length);
			HAL_UART_Transmit_IT(&WIFI_UART, (uint8_t *)sendQuantity, sizeof(sendQuantity));
			bCheckIfResponseReceivedOnTime("> ", WIFI_BUFFER, 5000);
			
			
			/*Send read request to the server to read the firmware*/
			clearWifiBufferAndResetItsIndex();
			
			HAL_UART_Transmit_IT(&WIFI_UART, (unsigned char *)tftpReadRequest, length);
		}
		else/*if not connected to the tftp server*/
		{
			#if TFTP_BOOTLOADER_DEBUG			
			printf("couldn't connect to TFTP server on Wifi\r\n");
			#endif
			
			char closeSocket[50];
			sprintf(closeSocket, "AT+CIPCLOSE=%i\r\n", WIFI_UDP_SOCKET_NO);
			HAL_UART_Transmit_IT(&WIFI_UART, (uint8_t*)closeSocket, strlen(closeSocket));
		}
	}	
}

/**
* @brief This function sends device firmware version to a web server using GSM. 
*				 Web server returns a file name if the device firmware version is NOT the most recent.
*/
void vAskFirmwareVersionRequestGSM(void)
{	
	if (WIFI_EXTERNAL_IP[0] == 0 && GSM_EXTERNAL_IP[0] != 0 && GSM_MODULE_STATE == GSM_STEADY_STATE)
	{
		if ((xBootloaderVariables.triggerUpdateAtStartGSM || xBootloaderVariables.askForUpdateCounter >= PERIODIC_FW_UPDATE_TIME))
		{
			/*Update should be requested at device start or because of the periodic update timer*/
			if (xBootloaderVariables.triggerUpdateAtStartGSM)
			{
				xBootloaderVariables.triggerUpdateAtStartGSM = false;
			} 
			else if(xBootloaderVariables.askForUpdateCounter >= PERIODIC_FW_UPDATE_TIME)/*reset the periodic requestor*/
			{
				xBootloaderVariables.askForUpdateCounter = 250000;
			}
			
			#if TFTP_BOOTLOADER_DEBUG
			printf("GSM is asking for update..\r\n");
			#endif
			
			
			char connectToTCPServer[100], connected[100];	
			
			HAL_FLASH_Unlock();
			
			clearGSMBufferAndResetItsIndex();
			
			sprintf(connectToTCPServer, "AT+QIOPEN=%i,%i,\"TCP\",%s,%i,%i,1\r\n", GSM_TCP_SOCKET_CONTEXT_ID, GSM_TCP_SOCKET_CONNECT_ID, FIRMWARE_VERSION_WEB_SERVER_ADDRESS, FIRMWARE_VERSION_WEB_SERVER_PORT, FIRMWARE_VERSION_WEB_SERVER_PORT);
			
			sprintf(connected, "+QIOPEN: %i,0\r\n", GSM_TCP_SOCKET_CONNECT_ID);
			
			/*connect to the TCP - Web server*/
			HAL_UART_Transmit_IT(&GSM_UART, (uint8_t*)connectToTCPServer, strlen(connectToTCPServer));
			
			if (bCheckIfResponseReceivedOnTime(connected, GSM_BUFFER, 15000))
			{
				char deviceVersionNumber[5], askFirmwareVersionURLPath[150], sendQuantity[150], closeSocket[50];
				
				clearGSMBufferAndResetItsIndex();
				
				
				vGetDeviceFirmwareVersion(deviceVersionNumber);		
				
				
				/*prepare the HTTP request to ask for update, send your version number to get if a new one*/
				sprintf(askFirmwareVersionURLPath, "%s%s%s", FIRMWARE_VERSION_WEB_SERVER_PATH_FIRST_PART, deviceVersionNumber, FIRMWARE_VERSION_WEB_SERVER_PATH_SECOND_PART);

				sprintf(sendQuantity, "AT+QISEND=%i,%i\r\n", GSM_TCP_SOCKET_CONNECT_ID, strlen(askFirmwareVersionURLPath));				
				
				
				HAL_UART_Transmit_IT(&GSM_UART, (uint8_t*)sendQuantity, strlen(sendQuantity));
				bCheckIfResponseReceivedOnTime("> ", GSM_BUFFER, 5000);
				clearGSMBufferAndResetItsIndex();
				
				
				HAL_UART_Transmit_IT(&GSM_UART, (uint8_t*)askFirmwareVersionURLPath, strlen(askFirmwareVersionURLPath));
				bCheckIfResponseReceivedOnTime("}}", GSM_BUFFER, 15000);
				
				
				/*parse the response*/
				vGetSubstringBetweenTwoStrings(GSM_BUFFER, "\"ip\":\"",   "\"", xBootloaderVariables.remoteIP);
				vGetSubstringBetweenTwoStrings(GSM_BUFFER, "\"port\":\"", "\"", xBootloaderVariables.remoteFixedPort);
				vGetSubstringBetweenTwoStrings(GSM_BUFFER, "\"file\":\"", "\"", xBootloaderVariables.fileName);
				
				/*if a recent update exists, web will return it as a filename*/
				if (strlen(xBootloaderVariables.fileName) > 0)
				{
					vGetSubstringBetweenTwoStrings(GSM_BUFFER, "rx-", "bin", xBootloaderVariables.newVersionNumber);
				}
				
				
				sprintf(closeSocket, "AT+QICLOSE=%i\r\n", GSM_TCP_SOCKET_CONNECT_ID);
				clearGSMBufferAndResetItsIndex();
				HAL_UART_Transmit_IT(&GSM_UART, (uint8_t*)closeSocket, strlen(closeSocket));
				bCheckIfResponseReceivedOnTime("OK\r\n", GSM_BUFFER, 5000);				
				
				
				#if TFTP_BOOTLOADER_DEBUG
				printf("GSM update ask request completed..\r\n");
				#endif
	
				
				vTFTPReadRequestQuectel(xBootloaderVariables.remoteIP, xBootloaderVariables.remoteFixedPort, xBootloaderVariables.fileName);
			}
			else
			{
				#if TFTP_BOOTLOADER_DEBUG
				printf("GSM could not connect to Web server to ask for firmware version..\r\n");
				#endif
			}
		}
	}
}

/**
* @brief  This function connects to the tftp server and sends a read request to the server over GSM
* @params char remoteIP[] 			 -> TFTP Server IP
*					char remoteFixedPort[] -> TFTP Server Port
*					char fileName[]				 -> name of the new firmware
*/
void vTFTPReadRequestQuectel(char remoteIP[], char remoteFixedPort[], char fileName[])
{
	if(remoteIP[0] != 0x00 && remoteFixedPort[0] != 0x00 && fileName[0] != 0x00)
	{
		char connectToUDPServer[100], connected[50];
		
		HAL_FLASH_Unlock();
		
		vEraseStorageSpace();
				
		clearGSMBufferAndResetItsIndex();
				
		sprintf(connectToUDPServer, "AT+QIOPEN=%i,%i,\"UDP SERVICE\",\"%s\",%s,69,1\r\n", GSM_UDP_SOCKET_CONTEXT_ID, GSM_UDP_SOCKET_CONNECT_ID, remoteIP, remoteFixedPort);
		
		/*connect to the UDP - TFTP server*/
		HAL_UART_Transmit_IT(&GSM_UART, (uint8_t *)connectToUDPServer, strlen(connectToUDPServer));
		
		sprintf(connected, "+QIOPEN: %i,0\r\n", GSM_UDP_SOCKET_CONNECT_ID);
		
		if (bCheckIfResponseReceivedOnTime(connected, GSM_BUFFER, 15000))
		{
			uint32_t length;
			
			char tftpReadRequest[50], sendQuantity[50];
			
			xBootloaderVariables.gsmBootloading = true;
			
			
			/*turn the access point off*/
			HAL_UART_Transmit_IT(&WIFI_UART, (uint8_t *)"AT+CWMODE=1\r\n", 	strlen("AT+CWMODE=1\r\n"));
			bCheckIfResponseReceivedOnTime("OK\r\n", GSM_BUFFER, 2500);
			clearWifiBufferAndResetItsIndex();	
			
			
			vPrepareTFTPReadRequest(tftpReadRequest, fileName, &length);
			
			
			sprintf(sendQuantity, "AT+QISEND=%i,%i,\"%s\",%s\r\n", GSM_UDP_SOCKET_CONNECT_ID, length, remoteIP, remoteFixedPort);
			HAL_UART_Transmit_IT(&GSM_UART, (uint8_t *)sendQuantity, strlen(sendQuantity));
			bCheckIfResponseReceivedOnTime("> ", GSM_BUFFER, 100);
			clearGSMBufferAndResetItsIndex();
			
			
			/*send the read request to the server*/
			HAL_UART_Transmit_IT(&GSM_UART, (unsigned char *)tftpReadRequest, length);
						
			xBootloaderVariables.solvePort = 1;

			xBootloaderVariables.startGSMTimeout = true;
		}
		else /*if not able to connect to server*/
		{
			#if TFTP_BOOTLOADER_DEBUG
			printf("Couldn't connect to TFTP server on GSM\r\n");
			#endif	
			
			char closeSocket[50];
			sprintf(closeSocket, "AT+QICLOSE=%i\r\n", GSM_TCP_SOCKET_CONNECT_ID);
			HAL_UART_Transmit_IT(&GSM_UART, (uint8_t*)closeSocket, strlen(closeSocket));
			clearGSMBufferAndResetItsIndex();
		}
	}
}

/**
* @brief This function parses incoming GSM buffer as the tftp data
*/
void vBootloaderQuectelEngage(void)
{
	/*if 10 msecs past from the last arrival data, means gsm responded*/
	if(GSM_TIME_PASSED_FROM_LAST_BYTE_RECEIVED >= 10 && GSM_BUFFER_RECEIVE_INDEX > 0)
	{		
		if (xBootloaderVariables.solvePort == 1)/*this is the first state supposed to resolve the port of the incoming data*/
		{
			for (int i = 0; i < sizeof(GSM_BUFFER); i++)
			{
				if (GSM_BUFFER[i]   == 0x72 
				 && GSM_BUFFER[i+1] == 0x65 
				 && GSM_BUFFER[i+2] == 0x63 
				 && GSM_BUFFER[i+3] == 0x76) /*if gsm buffer includes "recv" meaning that module arrived data*/
				{
					while (GSM_BUFFER[i] != 0x0D)/*place forward the buffer index until you see '\r'*/
					{
						i++;
					}
					i--;
					
					while (GSM_BUFFER[i] != 0x2C)/*place back the buffer index until you see ','*/
					{
						i--;
					}
					i++;
					
					while (GSM_BUFFER[i] != 0x0D)/*from ',' to '\r' resolve the characters as the port*/
					{
						char digit = GSM_BUFFER[i];
						xBootloaderVariables.remotePort = xBootloaderVariables.remotePort * 10 + atoi(&digit);
						i++;
					}
					xBootloaderVariables.solvePort = 2;/*once port is resolved, process continues over the second state where "xBootloaderVariables.solvePort == 2"*/
					i = sizeof(GSM_BUFFER);
				}
			}
		}
		
		if(xBootloaderVariables.solvePort == 2)
		{
			uint32_t dataIndex = 0;
			
			for (int i = 0; i < sizeof(GSM_BUFFER); i++)
			{
				if (GSM_BUFFER[i]   == 0x72 
				 && GSM_BUFFER[i+1] == 0x65 
				 && GSM_BUFFER[i+2] == 0x63 
				 && GSM_BUFFER[i+3] == 0x76) /*if gsm buffer includes "recv" meaning that module arrived data*/
				{
					while (GSM_BUFFER[i] != 0x2C)/*place the buffer index until you see ","*/
					{
						i++;
					}
					i++;
					
					while (GSM_BUFFER[i] != 0x2C)/*place the buffer index until you see the second ","*/
					{
						i++;
					}
					i++;
					
					while (GSM_BUFFER[i] != 0x2C)/*resolve the incoming data quantity*/
					{
						char digit = GSM_BUFFER[i];
						dataIndex = dataIndex * 10 + atoi(&digit);
						i++;
					}
											
					i = sizeof(GSM_BUFFER);/*conclude the loop*/
				}
			}
							
			for (int i = 0; i < sizeof(GSM_BUFFER); i++)
			{
				if (GSM_BUFFER[i]   == 0x72 
				 && GSM_BUFFER[i+1] == 0x65 
				 && GSM_BUFFER[i+2] == 0x63 
				 && GSM_BUFFER[i+3] == 0x76) /*if gsm buffer includes "recv" meaning that module arrived data*/
				{
					
					while (GSM_BUFFER[i] != 0x0A)/*place the buffer index until you see "\n"*/
					{
						i++;
					}
					i++;
					
					/*fulfil the tftp buffer from gsm buffer*/
					for (int k = 0; k < 516; k++)
					{
						xBootloaderVariables.currentTftpBuffer[k] = GSM_BUFFER[i];
						i++;
					}
					i = sizeof(GSM_BUFFER);/*conclude the loop*/
					
					vBootloaderCRC32ToFlash(dataIndex);
				}
			}
		}
	}
}

/**
* @brief This function parses incoming wifi buffer as the tftp data 
*/
void vBootloaderWifiEngage(void)
{
	for(int k = 0; k < sizeof(WIFI_BUFFER); k++)
	{
		if (WIFI_BUFFER[k]   == 0x49 &&
				WIFI_BUFFER[k+1] == 0x50 &&
				WIFI_BUFFER[k+2] == 0x44 &&
				WIFI_BUFFER[k+3] == 0x2C &&
				WIFI_BUFFER[k+4] == (0x30 + WIFI_UDP_SOCKET_NO))/*if "IPD, X" arrives at the wifi buffer, meaning that Xth socket arrived data, which is the udp socket*/
		{			
			
			uint32_t dataIndex = 0;
			
			for (int i = 0; i < sizeof(WIFI_BUFFER); i++)
			{
				if (WIFI_BUFFER[i] == 0x3A)/*:*/
				{
					if (WIFI_BUFFER[i-1] == 0x36 &&
							WIFI_BUFFER[i-2] == 0x31 &&
							WIFI_BUFFER[i-3] == 0x35)/*if arrival data includes "516:" meaning that 516 bytes of tftp package arrived*/
					{
						i++;
						
						for (int j = 0; j < 516; j++)
						{
							xBootloaderVariables.currentTftpBuffer[j] = WIFI_BUFFER[i];/*transfer the tftp package from the wifi buffer to the tftp buffer*/
							i++;
						}
						
						i = sizeof(WIFI_BUFFER);
						
						dataIndex = 516;
						
						vBootloaderCRC32ToFlash(dataIndex);
					}
					else/* if the last package; namely, package arrival quantity is NOT 516 but different*/
					{
						/*resolve how many data arrived*/
						if(WIFI_BUFFER[i-2] == 0x2C || WIFI_BUFFER[i-3] == 0x2C || WIFI_BUFFER[i-4] == 0x2C)/*arrival quantity differ from 1 to 3 digits in the context of TFTP nax buf size 512*/
						{
							if(WIFI_BUFFER[i-1] != 0x2C)
							{
								char digit = WIFI_BUFFER[i-1];
								dataIndex = atoi(&digit);
								if(WIFI_BUFFER[i-2] != 0x2C)
								{
									char digit = WIFI_BUFFER[i-2];
									dataIndex += atoi(&digit)*10;
									if(WIFI_BUFFER[i-3] != 0x2C)
									{
										char digit = WIFI_BUFFER[i-3];
										dataIndex += atoi(&digit)*100;
									}
								}
							}
							
							i++;
														
							for(int j = 0; j < dataIndex; j++)/*transfer the data into tftp buffer*/
							{
								xBootloaderVariables.currentTftpBuffer[j] = WIFI_BUFFER[i];
								i++;
							}
							
							i = sizeof(WIFI_BUFFER);
							
							vBootloaderCRC32ToFlash(dataIndex);
						}
					}
				}
			}			
			k = sizeof(WIFI_BUFFER);/*conclude the loop*/
		}
	}
}

/**
* @brief This function puts tftp buffer to crc calculation buffer, calculates its checksum and writes it to the flash
* @param uint32_t tftpBufferIndex -> telling how full the current tftp buffer is
*/
void vBootloaderCRC32ToFlash(uint32_t tftpBufferIndex)
{		
	xBootloaderVariables.incomingBlockNumber = xBootloaderVariables.currentTftpBuffer[2]*(0x100) + xBootloaderVariables.currentTftpBuffer[3];
	
	if ((xBootloaderVariables.incomingBlockNumber == xBootloaderVariables.incomingBlockNumberOld + 1) && xBootloaderVariables.incomingBlockNumber > 1)
	{	
		xBootloaderVariables.TFTPTimeoutCounter = 0;	
		
		xBootloaderVariables.incomingBlockNumberOld = xBootloaderVariables.incomingBlockNumber;
		
		vPrintTFTPBlockNumber(xBootloaderVariables.incomingBlockNumber, true);
		
		if(!bIsTheLastTFTPPackage(tftpBufferIndex))
		{
			vFlashTFTPBuffer(xBootloaderVariables.previousTftpBuffer, tftpBufferIndex);
			
			vCalculateCyclicCRC32(&xBootloaderVariables.checkSumCalculated, xBootloaderVariables.previousTftpBuffer, sizeof(xBootloaderVariables.previousTftpBuffer) - 4);
			
			memcpy(xBootloaderVariables.previousTftpBuffer, xBootloaderVariables.currentTftpBuffer, sizeof(xBootloaderVariables.previousTftpBuffer));
			
			vTFTPIncrementACK(xBootloaderVariables.ACK);
			
			vTFTPSendAcknowledge((char *)xBootloaderVariables.ACK, sizeof(xBootloaderVariables.ACK));
		}
		else
		{
			uint32_t crcIndex = 0;
			
			if (tftpBufferIndex == 4)/*if last package does not contain data*/
			{
				vFlashTFTPBuffer(xBootloaderVariables.previousTftpBuffer, sizeof(xBootloaderVariables.previousTftpBuffer) - 4);
				
				vExtractCRCFromTheLastTFTPPackage(&xBootloaderVariables.checkSumOnTheLastTFTPPackage, xBootloaderVariables.previousTftpBuffer, sizeof(xBootloaderVariables.previousTftpBuffer), &crcIndex);

				vTFTPIncrementACK(xBootloaderVariables.ACK);
				
				vTFTPSendAcknowledge((char *)xBootloaderVariables.ACK, sizeof(xBootloaderVariables.ACK));
				
				vCalculateCyclicCRC32(&xBootloaderVariables.checkSumCalculated, xBootloaderVariables.previousTftpBuffer, crcIndex + 1);
				
				vEvaluateCRC32(xBootloaderVariables.checkSumCalculated, xBootloaderVariables.checkSumOnTheLastTFTPPackage);
			}
			else
			{				
				vFlashTFTPBuffer(xBootloaderVariables.previousTftpBuffer, sizeof(xBootloaderVariables.previousTftpBuffer));
				
				vCalculateCyclicCRC32(&xBootloaderVariables.checkSumCalculated, xBootloaderVariables.previousTftpBuffer, sizeof(xBootloaderVariables.previousTftpBuffer) - 4);
																
				vFlashTFTPBuffer(xBootloaderVariables.currentTftpBuffer, tftpBufferIndex - 4);
				
				vExtractCRCFromTheLastTFTPPackage(&xBootloaderVariables.checkSumOnTheLastTFTPPackage, xBootloaderVariables.currentTftpBuffer, tftpBufferIndex, &crcIndex);
				
				vTFTPIncrementACK(xBootloaderVariables.ACK);
				
				vTFTPSendAcknowledge((char *)xBootloaderVariables.ACK, sizeof(xBootloaderVariables.ACK));
				
				vCalculateCyclicCRC32(&xBootloaderVariables.checkSumCalculated, xBootloaderVariables.currentTftpBuffer, crcIndex + 1);
				
				vEvaluateCRC32(xBootloaderVariables.checkSumCalculated, xBootloaderVariables.checkSumOnTheLastTFTPPackage);
			}
		}
	}
	else if((xBootloaderVariables.incomingBlockNumber == xBootloaderVariables.incomingBlockNumberOld + 1) && xBootloaderVariables.incomingBlockNumber == 1)
	{
		xBootloaderVariables.TFTPTimeoutCounter = 0;		
		
		xBootloaderVariables.incomingBlockNumberOld = xBootloaderVariables.incomingBlockNumber;
		
		vPrintTFTPBlockNumber(xBootloaderVariables.incomingBlockNumber, true);
		
		memcpy(xBootloaderVariables.previousTftpBuffer, xBootloaderVariables.currentTftpBuffer, sizeof(xBootloaderVariables.previousTftpBuffer));
		
		vTFTPIncrementACK(xBootloaderVariables.ACK);
		
		vTFTPSendAcknowledge((char *)xBootloaderVariables.ACK, sizeof(xBootloaderVariables.ACK));
	}
	else
	{				
		vPrintTFTPBlockNumber(xBootloaderVariables.incomingBlockNumber, false);
		
		xBootloaderVariables.incomingBlockNumber = xBootloaderVariables.incomingBlockNumberOld ;
		
		vTFTPSendAcknowledge((char *)xBootloaderVariables.ACK, sizeof(xBootloaderVariables.ACK));
	}
}

/**
* @brief  This function sends acknowledge to the server per incoming package
*         If its the last package, function checks for the CRC matching and if it is OK, function writes the firmware version to the flash
* @params char ack[]    -> acknowledge buffer to be sent
*					uint32_t size -> size of the acknowledge buffer
*/
void vTFTPSendAcknowledge(char ack[], uint32_t size)
{	
	char sendQuantity[50];
	
	if(xBootloaderVariables.wifiBootloading)
	{
		clearWifiBufferAndResetItsIndex();
		
		sprintf(sendQuantity, "AT+CIPSEND=%i,%i\r\n", WIFI_UDP_SOCKET_NO, size);
				
		HAL_UART_Transmit_IT(&WIFI_UART, (uint8_t *)sendQuantity, sizeof(sendQuantity));
		
		bCheckIfResponseReceivedOnTime("> ", WIFI_BUFFER, 100);
		
		HAL_UART_Transmit_IT(&WIFI_UART, (uint8_t *)ack, size);
	}
	else if (xBootloaderVariables.gsmBootloading)
	{
		clearGSMBufferAndResetItsIndex();
		
		sprintf(sendQuantity, "AT+QISEND=%i,%i,\"%s\",%i\r\n", GSM_UDP_SOCKET_CONNECT_ID, size, xBootloaderVariables.remoteIP, xBootloaderVariables.remotePort);
		
		HAL_UART_Transmit_IT(&GSM_UART, (uint8_t *)sendQuantity, strlen(sendQuantity));
		
		bCheckIfResponseReceivedOnTime("> ", GSM_BUFFER, 100);
		
		HAL_UART_Transmit_IT(&GSM_UART, (uint8_t *)ack, size);
	}
}

/**
* @brief If 516 bytes of data arrived, it is not the last package of TFTP.
* @params char tftpBuffer[]					-> input buffer to write into flash
*					uint32_t tftpBufferIndex  -> how many bytes to be written
*/
void vFlashTFTPBuffer(char tftpBuffer[], uint32_t tftpBufferIndex)
{
	uint32_t dataToBeWrittenToTheFlash = 0;
	
	for (int i = 1; i < tftpBufferIndex/4; i++)
	{
		/*data will be written to the flash using byte shifted addition*/
		dataToBeWrittenToTheFlash = 
			tftpBuffer[4*i+3]*0x1000000 + 
			tftpBuffer[4*i+2]*0x10000 + 
			tftpBuffer[4*i+1]*0x100 + 
			tftpBuffer[4*i];
		
		if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, xBootloaderVariables.applicationStoredAddressEnd, dataToBeWrittenToTheFlash) != HAL_OK)
		{
			#if TFTP_BOOTLOADER_DEBUG
			printf("System Reset: TFTP data could not be written to the flash!\r\n");
			#endif
				
			NVIC_SystemReset();
		}
		
		xBootloaderVariables.applicationStoredAddressEnd += 4;
	}
}

/**
* @brief  CRC32 table for calculation mapping
* @retval chosen array value
*/
const uint32_t crc32_tab[] = 
{
	0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
	0xe963a535, 0x9e6495a3,	0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
	0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
	0xf3b97148, 0x84be41de,	0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
	0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec,	0x14015c4f, 0x63066cd9,
	0xfa0f3d63, 0x8d080df5,	0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
	0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,	0x35b5a8fa, 0x42b2986c,
	0xdbbbc9d6, 0xacbcf940,	0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
	0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
	0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
	0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,	0x76dc4190, 0x01db7106,
	0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
	0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
	0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
	0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
	0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
	0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
	0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
	0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
	0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
	0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
	0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
	0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
	0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
	0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
	0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
	0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
	0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
	0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
	0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
	0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
	0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
	0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
	0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
	0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
	0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
	0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
	0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
	0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
	0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
	0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
	0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
	0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

/**
* @brief  This function calculates CRC32 checksum of an input buffer
* @param  const void *buf --> The iput buffer that its crc32 is to be calculated
*					size_t size     --> Size of the input buffer
*				  uint32_t init   --> Result of the previous crc32 calculation. If it is the first calculation, parameter should be entered 0.
* @retval Result of the crc32 calculation.
* @note   You can notice that 'p' is incremented 4 times before the process because, first 4 bytes of tftpBuffer is TFTP OP code and the rest is data
*/
uint32_t crc32(const void *buf, size_t size, uint32_t init)
{
	const uint8_t *p = buf;
	*p++; *p++; *p++; *p++;
	uint32_t crc = ~init;
	while (size--)
	crc = crc32_tab[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
	return crc ^ ~0U;
}

/**
* @brief This function extracts CRC32 value from the last TFTP package
* @params char tftpBuffer[]					-> input buffer to extract CRC32 value from the end of that buffer
*					uint32_t tftpBufferIndex  -> the end index of the input buffer where CRC32 yields
*					uint32_t *crcBufferIndex	-> To calculate the last package's crc32 dependency
*/
void vExtractCRCFromTheLastTFTPPackage(uint32_t *checsumExtracted, char tftpBuffer[], uint32_t tftpBufferIndex, uint32_t *crcBufferIndex)
{	
	for (int i = 1; i < ((tftpBufferIndex)/4); i++)
	{				
		if(i == ((tftpBufferIndex)/4)-1)
		{
			*checsumExtracted = 
				tftpBuffer[4*i]  *0x1000000 + 
				tftpBuffer[4*i+1]*0x10000 + 
				tftpBuffer[4*i+2]*0x100 + 
				tftpBuffer[4*i+3];
		}
		else
		{
			*crcBufferIndex = 4*(i-1) + 3;
		}			
	}
}

/**
* @brief This function calculates the crc32 in a cyclic way over writing the *calculatedCRC32 value
* @params uint32_t *calculatedCRC32 -> cyclically calculated CRC32
*					char tftpBuffer[]					-> input buffer to calculate CRC32 over
*					uint32_t size							-> size of the input buffer
*/
void vCalculateCyclicCRC32(uint32_t *calculatedCRC32, char tftpBuffer[], uint32_t size)
{
	if (size != 1)
	{
		*calculatedCRC32 = crc32((const void *)tftpBuffer, size, *calculatedCRC32);
	}
}

/**
* @brief This function compares the crc32s and writes new firmware version to the flash
*/
void vEvaluateCRC32(uint32_t crcCalculated, uint32_t crcGiven)
{
	#if TFTP_BOOTLOADER_DEBUG
	printf("Size of the new app is = %d bytes \r\n", 	 xBootloaderVariables.applicationStoredAddressEnd - xBootloaderVariables.applicationStoredAddressStart);
	printf("LAST checksum calculated:     0x%08x\r\nChecksum value on the memory: 0x%08x\r\n", crcCalculated, crcGiven);
	#endif
	
	if(crcCalculated == crcGiven)
	{
		#if TFTP_BOOTLOADER_DEBUG
		printf("CRC32 check is OK. \r\n\r\n");
		#endif
		
		vFlashChecksumAndFirmwareVersion();
		
		NVIC_SystemReset();
	}
	else
	{
		#if TFTP_BOOTLOADER_DEBUG
		printf("CRC32 check FAILED. \r\n\r\n");
		#endif
		
		vEraseStorageSpace();
		
		SAVE_ENERGY_REGISTERS();
		
		NVIC_SystemReset();
	}
}

/**
* @brief This function erases chosen flash sector 
* @param Sector identifying number
*/
void vFlashEraseSector(uint32_t sectorNo)
{
	HAL_StatusTypeDef HAL = HAL_ERROR;
	FLASH_EraseInitTypeDef EraseInitStruct;
	uint32_t SECTORError;
	
	HAL_FLASH_Unlock();
	
	EraseInitStruct.Sector        = sectorNo;
	EraseInitStruct.TypeErase     = FLASH_TYPEERASE_SECTORS;
	EraseInitStruct.VoltageRange  = FLASH_VOLTAGE_RANGE_3;
	EraseInitStruct.NbSectors     = 1;
		
	while (HAL != HAL_OK) 
	{
		HAL = HAL_FLASHEx_Erase(&EraseInitStruct, &SECTORError);
		
		if (HAL != HAL_OK){for(int i = 0; i < 5000; i++){}}
	}
}

/**
* @brief This function copies storage space to application space
*/
void vCopyStorageSpaceToApplicationSpace(uint32_t appSpace, uint32_t storageSpace, uint32_t spaceSize)
{
	uint32_t writeAddress = appSpace, copyAddress  = storageSpace, data = 0;
	
	for(int i = 0; i < (spaceSize/4); i++)
	{
		data = *(__IO uint32_t *)copyAddress;
		
		if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, writeAddress, data) != HAL_OK)
		{
			NVIC_SystemReset();
		}
		
		writeAddress += 4;
		copyAddress  += 4;
	}
}

/**
* @brief This function flashes firmware version and '1' to the end of that sector showing checksum of the firmware is approved
*/
void vFlashChecksumAndFirmwareVersion(void)
{
	/*checksum correction bit held and written to the end of the storage sector*/
	if(HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, STORAGE_ADDRESS + MAX_APPICATION_SIZE - 4, 0x01) != HAL_OK)
	{
		#if TFTP_BOOTLOADER_DEBUG
		printf("System Reset: CRC32 approval bit could not written to the flash\r\n");
		#endif
		
		NVIC_SystemReset();
	}
	
	/*new version number written to the end of the storage sector*/
	for(int i = 0; i < 5; i++)
	{
		if(HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, (STORAGE_ADDRESS + MAX_APPICATION_SIZE - (24 - i*4)), xBootloaderVariables.newVersionNumber[i]) != HAL_OK)
		{
			#if TFTP_BOOTLOADER_DEBUG
			printf("System Reset: Version number bits could not be written to the flash\r\n");
			#endif
			
			NVIC_SystemReset();
		}
	}
}

/**
* @brief This function checks the current firmware version of the device
* @param char array to be filled
*/
void vGetDeviceFirmwareVersion(char firmwareVersion[])
{
	uint32_t versionAddress = APPLICATION_ADDRESS + MAX_APPICATION_SIZE - 24;
	
	/*obtain the device firmware version*/
	for (int i = 0; i < 5; i++)
	{
		if ((int)*(__IO uint32_t *)versionAddress != -1)
		{
			firmwareVersion[i] = *(__IO uint32_t *)versionAddress;
			versionAddress += 4;
		}
		else
		{
			sprintf(firmwareVersion ,CURRENT_FW_VER);
			versionAddress += 4;
		}
	}
}

/**
* @brief This function erases the storage space, modify the function to your needs. (etc. new mcu different sectors..)
*/
void vEraseStorageSpace(void)
{
	vFlashEraseSector (7);
}

/**
* @brief This function erases the application space, modify the function to your needs (etc. new mcu different sectors..)
*/
void vEraseApplicationSpace(void)
{
	vFlashEraseSector (6);
}

/**
* @brief This function jumps to application space
*/
void vBootloaderJumpToApplication(uint32_t appSpace)
{
	typedef void (*pFunction)(void);
	
	uint32_t  JumpAddress = *(__IO uint32_t*)(appSpace + 4);
	
	pFunction Jump = (pFunction)JumpAddress;
	
	HAL_RCC_DeInit();
	
	HAL_DeInit();
	
	__set_MSP(*(__IO uint32_t*)appSpace);
	
	Jump();
}

/**
* @brief This function operates bootloader timeouts at systick (1ms periodically ISR called interrupt)
*/
void vBootloadercallOver1ms(void)
{
	xBootloaderVariables.askForUpdateCounter++;
	
	if(xBootloaderVariables.startGSMTimeout && GSM_BUFFER_RECEIVE_INDEX != 0) GSM_TIME_PASSED_FROM_LAST_BYTE_RECEIVED++;
	
	if (xBootloaderVariables.wifiBootloading || xBootloaderVariables.gsmBootloading)/*starts to be processed when TFTP communication starts*/
	{	
		xBootloaderVariables.startTFTPTimeout = true;
		
		xBootloaderVariables.connectionCounter++;

		if (xBootloaderVariables.startTFTPTimeout)
		{
			xBootloaderVariables.TFTPTimeoutCounter++;
		}
	}
}

/**
* @brief This function checks bootloader timeouts at a thread
*/
void vBootloaderProcessTimers(void)
{
	if(xBootloaderVariables.connectionCounter >= 5000000)/*if connection is over 5000 seconds, corrupt*/
	{
		xBootloaderVariables.connectionCounter = 0;
		
		SAVE_ENERGY_REGISTERS();
		
		NVIC_SystemReset();
	}
	
	if (xBootloaderVariables.TFTPTimeoutCounter >= 40000)/*if wrong package is arriving over 40 secs, corrupt*/
	{
		SAVE_ENERGY_REGISTERS();
		
		NVIC_SystemReset();
	}
}

/**
* @brief This function prepares the tftp read request
* @param char readRequest[] -> the request to be sent over TFTP
*				 char fileName[]  	-> file name to be inserted into the request
*				 uint32_t length	  -> length of the request
*/
void vPrepareTFTPReadRequest(char readRequest[], char fileName[], uint32_t *length)
{
	uint32_t index = 2;
		
	readRequest[0]  = 0x0;
	readRequest[1]  = 0x1;/*these 2 bytes are operation codes of TFTP to read*/
	
	for (index = 2; index < strlen(fileName) + 2; index++)
	{
		readRequest[index] = fileName[index-2];
	}
	
	readRequest[index++] = 0x00;
	readRequest[index++] = 0x6F;
	readRequest[index++] = 0x63;
	readRequest[index++] = 0x74;
	readRequest[index++] = 0x65;
	readRequest[index++] = 0x74;
	readRequest[index++] = 0x00;/*parse the file name got from web server to the request*/
	
	*length = index;
}

/**
* @brief  This function increments acknowledge number agains incoming block
* @params uint8_t ACK[] is a 4 byte acknowledge array, first 2 bits are 0 and 4, rest will be incrementing as follows:
*/
void vTFTPIncrementACK(uint8_t ACK[])
{
	if(ACK[3] == 0xFF)
	{
		ACK[3] = 0;
		ACK[2]++;
	}
	else
	{
		ACK[3]++;
	}
}

/**
* @brief  This function prints block numbers of tftp packages
*	@params uint32_t blockNumber    -> to be printed
*					bool correctOrIncorrect -> if the arrival block number is correct or incorrect
*/
void vPrintTFTPBlockNumber(uint32_t blockNumber, bool correctOrIncorrect)
{
	#if TFTP_BOOTLOADER_DEBUG
	if (correctOrIncorrect)
	{
		printf("incoming block number:       %d\r\n", blockNumber);
	}
	else
	{
		printf("WRONG incoming block number: %d\r\n", blockNumber);
	}
	#endif
}

/**
* @brief  If 516 bytes of data arrived, it is not the last package of TFTP.
* @params uint32_t packageLength			-> package length of the arrival package
* @retval true if it is the last package, false otherwise
*/
bool bIsTheLastTFTPPackage(uint32_t packageLength)
{
	if (packageLength != 516)
	{
		return true;
	}
	else
	{
		return false;
	}
}

/**
* @brief This function checks the input buffer including a given response in a timeout
* @param char expectedResponse[] -> expected response such as "OK", "BUSY" or else
*				 char inputBuffer[] 		 -> buffer to be checked
*				 uint32_t timeout 			 -> desired timeout in ms
*/
bool bCheckIfResponseReceivedOnTime(char expectedResponse[], char inputBuffer[], uint32_t timeout)
{	
	uint32_t timeCount = 0;
	
	while(strstr(inputBuffer, expectedResponse) == NULL && timeCount < timeout)
	{
		WATCHDOG_RESET();
		
		HAL_Delay(1);
		
		timeCount++;
	}
	
	if (strstr(inputBuffer, expectedResponse) != NULL)
	{
		WATCHDOG_RESET();
		
		#if TFTP_BOOTLOADER_DEBUG
		printf("expected response: %s returned 1\r\n", expectedResponse);
		#endif
		
		return true;
	}
	else
	{
		WATCHDOG_RESET();
		
		#if TFTP_BOOTLOADER_DEBUG
		printf("expected response: %s returned 0\r\n", expectedResponse);
		#endif
		
		return false;
	}
}

/**
* @brief This function reduces Wifi Baud Rate from 115200 to 19200
*/
void vReduceWifiBaudRateTo19200(void)
{
	clearWifiBufferAndResetItsIndex();
	
	HAL_UART_Transmit_IT(&WIFI_UART, (uint8_t *)"AT+UART_CUR=19200,8,1,0,1\r\n", sizeof("AT+UART_CUR=19200,8,1,0,1\r\n"));
		
	if (bCheckIfResponseReceivedOnTime("OK\r\n", WIFI_BUFFER, 500))
	{
		vUsartReInit(&WIFI_UART, 19200, NULL, NULL, NULL);
		
		HAL_UART_Receive_IT(&WIFI_UART, &WIFI_UART_RECEIVED_CHARACTER, 1);
	}
	
	clearWifiBufferAndResetItsIndex();
}

/**
* @brief This function initializes the bootloader variables
*/
void vBootloadervariablesInit(void)
{
	xBootloaderVariables.ACK[0] = 0; 
	xBootloaderVariables.ACK[1] = 4; 
	xBootloaderVariables.ACK[2] = 0; 
	xBootloaderVariables.ACK[3] = 0;
	
	HAL_FLASH_Unlock();
	
	xBootloaderVariables.applicationStoredAddressStart = STORAGE_ADDRESS;
	
	xBootloaderVariables.applicationStoredAddressEnd = xBootloaderVariables.applicationStoredAddressStart;
}
