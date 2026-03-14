/*
 * =====================================================================================
 * File Name:    offlineControl.c
 * Author:       Vanix Technologies PVT. LTD.
 * Date Created: 2025-04-02
 * Description:  This header file provides a wrapper for saving data into qspi flash
 *               using fatfs file system. It wil, contain apis for saving data on flash
 *               for sessions when device is offiline and send all data to clound when
 *               device gets online.
 *
 *               Features:
 *               - Enhanced debugging with logging
 *
 * Revision History:
 * Version 1.0 - 2025-04-02 - Initial Implementations
 * =====================================================================================
 */

/*
 * =====================================================================================
 * Include Headers
 * =====================================================================================
 */

#include "stdlib.h"
#include "stdio.h"
#include "ff.h"
#include "xil_printf.h"

#include "qspiControl.h"
#include "offlineControl.h"
#include "evseMainApp.h"

/*
 * =====================================================================================
 * Global Variables
 * =====================================================================================
 */

TaskHandle_t xOfflineControlTask;						// CP Task handler pointer
extern FATFS FatFs;						/* Global variable fot fatfs file system */

extern EVSEStateMachineStatus_e g_EVSECurrentState;	/* Global variable to track the EVSE current state status */

/*
 * =====================================================================================
 * Static Function Definitions
 * =====================================================================================
 */

/**
 * @brief Delete a sub-directory even if it contains any file.
 *
 * This function Delete a sub-directory even if it contains any file.
 * Function is taken from examples given on fatfs webpage. *
 * Note : The delete_node() function is for R0.12+, and It works regardless of FF_FS_RPATH.
 */
static FRESULT delete_node (
    TCHAR* path,    /* Path name buffer with the sub-directory to delete */
    UINT sz_buff,   /* Size of path name buffer (items) */
    FILINFO* fno    /* Name read buffer */
)
{
    UINT i, j;
    FRESULT fr;
    DIR dir;


    fr = f_opendir(&dir, path); /* Open the sub-directory to make it empty */
    if (fr != FR_OK) return fr;

    for (i = 0; path[i]; i++) ; /* Get current path length */
    path[i++] = _T('/');

    for (;;) {
        fr = f_readdir(&dir, fno);  /* Get a directory item */
        if (fr != FR_OK || !fno->fname[0]) break;   /* End of directory? */
        j = 0;
        do {    /* Make a path name */
            if (i + j >= sz_buff) { /* Buffer over flow? */
                fr = 100; break;    /* Fails with 100 when buffer overflow */
            }
            path[i + j] = fno->fname[j];
        } while (fno->fname[j++]);
        if (fno->fattrib & AM_DIR) {    /* Item is a sub-directory */
            fr = delete_node(path, sz_buff, fno);
        } else {                        /* Item is a file */
            fr = f_unlink(path);
        }
        if (fr != FR_OK) break;
    }

    path[--i] = 0;  /* Restore the path name */
    f_closedir(&dir);

    if (fr == FR_OK) fr = f_unlink(path);  /* Delete the empty sub-directory */
    return fr;
}

/**
 * @brief Find filename for given filetype.
 *
 * This function finds filename for given filetype and fill into filename.
 */
static int GetFileName(int filetype, int IsPointer, int sessionid, char *filename)
{
	u8 retVal = XST_SUCCESS;

	if(IsPointer)
	{
		if(filetype == FILETYPE_STATUS)
		{
			sprintf(filename,"%d/%s",sessionid,FILENAME_READPOINTER_STATUS_NOTFICATION);
		}
		else if(filetype == FILETYPE_TRANSACTION_START)
		{
			sprintf(filename,"%d/%s",sessionid,FILENAME_READPOINTER_TRANSACTION_START);
		}
		else if(filetype == FILETYPE_TRANSACTION_STOP)
		{
			sprintf(filename,"%d/%s",sessionid,FILENAME_READPOINTER_TRANSACTION_STOP);
		}
		else if(filetype == FILETYPE_METERVAL_PROG_UPDATE)
		{
			sprintf(filename,"%d/%s",sessionid,FILENAME_READPOINTER_METERVAL_PROG_UPDATE);
		}
		else if(filetype == FILETYPE_METERVAL_SUMMARY_UPDATE)
		{
			sprintf(filename,"%d/%s",sessionid,FILENAME_READPOINTER_METERVAL_SUMMARY_UPDATE);
		}
		else if(filetype == FILETYPE_GENERAL_STATUS)
		{
			sprintf(filename,"%s",FILENAME_READPOINTER_GENERAL_STATUS_NOTFICATION);
		}
		else if(filetype == FILETYPE_RFID_LOG)
		{
			sprintf(filename,"%s",FILENAME_READPOINTER_RFID_LOG);
		}
		else
		{
			xil_printf("%s,invalid file type, file type %d\n\r",__func__,filetype);
			retVal = XST_FAILURE;
		}
	}
	else
	{
		if(filetype == FILETYPE_STATUS)
		{
			sprintf(filename,"%d/%s",sessionid,FILENAME_STATUS_NOTIFICATION);
		}
		else if(filetype == FILETYPE_TRANSACTION_START)
		{
			sprintf(filename,"%d/%s",sessionid,FILENAME_TRANSACTION_START);
		}
		else if(filetype == FILETYPE_TRANSACTION_STOP)
		{
			sprintf(filename,"%d/%s",sessionid,FILENAME_TRANSACTION_STOP);
		}
		else if(filetype == FILETYPE_METERVAL_PROG_UPDATE)
		{
			sprintf(filename,"%d/%s",sessionid,FILENAME_METERVAL_PROG_UPDATE);
		}
		else if(filetype == FILETYPE_METERVAL_SUMMARY_UPDATE)
		{
			sprintf(filename,"%d/%s",sessionid,FILENAME_METERVAL_SUMMARY_UPDATE);
		}
		else if(filetype == FILETYPE_GENERAL_STATUS)
		{
			sprintf(filename,"%s",FILENAME_GENERAL_STATUS_NOTIFICATION);
		}
		else if(filetype == FILETYPE_RFID_LOG)
		{
			sprintf(filename,"%s",FILENAME_RFID_LOG);
		}
		else
		{
			xil_printf("%s,invalid file type, file type %d\n\r",__func__,filetype);
			retVal = XST_FAILURE;
		}
	}
	return retVal;
}

/**
 * @brief Print directory structure with all available files for given path and level.
 *
 */
static void print_directory_structure(const char* path, int level)
{
    DIR dir;
    FILINFO file_info;
    int ret;
    int i;

    ret = f_opendir(&dir, path);

    if (ret == FR_OK) {
        while (1) {
            ret = f_readdir(&dir, &file_info);

            if (ret != FR_OK || file_info.fname[0] == 0) {
                break;
            }

            // Print indentation based on level
            for (i = 0; i < level; i++) {
            	xil_printf("  ");
            }

            xil_printf("%s\r\n", file_info.fname);

            if (file_info.fattrib & AM_DIR) {
                // It's a directory, so recursively print its contents
                char new_path[FF_MAX_LFN + strlen(path) + 2];
                snprintf(new_path, sizeof(new_path), "%s%s/", path, file_info.fname); // Construct path
                print_directory_structure(new_path, level + 1);
            }
        }
    } else {
    	xil_printf("Error opening directory: %d\n", ret);
    }

    f_closedir(&dir);
}

/*
 * =====================================================================================
 * Global Function Definitions
 * =====================================================================================
 */

/**
 * @brief Read current read position for provided file type flash.
 *
 * This function reads position for provided file type flash.
 * If file doesnt exist it will create and init with 0.
 */
int flashGetCurrentReadPosition(int filetype,int sessionid,int *PositionValue)
{
	u8 retVal = XST_FAILURE;
	FIL fil;							/* File object */
	FRESULT ret;						/* Return variable for fatfs api return */
	FILINFO fno;
	char filename[100] = "";

	if(GetFileName(filetype,1,sessionid,filename) == XST_FAILURE)
	{
		xil_printf("%s,invalid file type, file type %d\n\r",__func__,filetype);
		return retVal;
	}

	/* Check if file exists */
	ret = f_stat(filename, &fno);

	/* if file doesnt exists */
	if(ret != FR_OK)
	{
		xil_printf("%s,file %s doesnt exist, file type %d, creating now\n\r",__func__,filename,filetype);

		/* Open a read position file for write operation */
		ret = f_open(&fil, filename, FA_CREATE_ALWAYS | FA_WRITE);
		if (ret == FR_OK)
		{
			/* write  on flash */
			char buffWrite[3] = "0";
			unsigned int writecount = 0;

			if (f_write(&fil, buffWrite, strlen(buffWrite), &writecount) == FR_OK)
			{
				retVal = XST_SUCCESS;
			}

			/* Closing file */
			f_close(&fil);
		}
		else
		{
			xil_printf("%s,opening current position file for writing failed!, file type %d, error %d\n\r",__func__,filetype,ret);
		}
		return retVal;
	}

	/* As file exists, open it for reading */
	ret = f_open(&fil, filename, FA_OPEN_EXISTING | FA_READ);
	if (ret == FR_OK)
	{
		/* Read buffer from file */
		char buffRead[10] = "";
		unsigned int readcount = 0;
		if (f_read(&fil, buffRead, sizeof(buffRead), &readcount) == FR_OK)
		{
			/* update Sessionid */
			*PositionValue = atoi(buffRead);
			retVal = XST_SUCCESS;
		}
		else
		{
			xil_printf("%s,reading current position file failed!, file type %d, ret %d\n\r",__func__,filetype,ret);
		}

		/* Closing file */
		f_close(&fil);
	}
	else
	{
		xil_printf("%s,opening file for reading failed!, file type %d, ret %d\n\r",__func__,filetype,ret);
	}

	return retVal;
}

/**
 * @brief Write read position for provided file type flash.
 *
 * This function updates read position value for provided file type flash.
 */
int flashWriteReadPosition(int filetype, int sessionid, int PositionValue)
{
	u8 retVal = XST_FAILURE;
	FIL fil;												/* File object */
	FRESULT ret = FR_INVALID_PARAMETER;						/* Return variable for fatfs api return */
	char filename[100] = "";

	if(GetFileName(filetype,1,sessionid,filename) == XST_FAILURE)
	{
		xil_printf("%s,invalid file type, file type %d\n\r",__func__,filetype);
		return retVal;
	}

	/* Open a read position file for write operation */
	ret = f_open(&fil, filename, FA_CREATE_ALWAYS | FA_WRITE);
	if (ret == FR_OK)
	{
		/* save updated id on flash */
		char buffWrite[10] = "";
		unsigned int writecount = 0;

		itoa(PositionValue,buffWrite,10);
		if (f_write(&fil, buffWrite, strlen(buffWrite), &writecount) == FR_OK)
		{
			retVal = XST_SUCCESS;
		}

		/* Closing file */
		f_close(&fil);
	}
	else
	{
		xil_printf("%s,opening read position file for writing failed!, ret %d\n\r",__func__,ret);
	}

	return retVal;
}

/**
 * @brief Update current session id saved on flash.
 *
 * This function increments current session id saved on flash.
 * Called when new session starts.
 */
int flashUpdateCurrentSessionID()
{
	u8 retVal = XST_FAILURE;
	FIL fil;							/* File object */
	FRESULT ret;						/* Return variable for fatfs api return */
	FILINFO fno;

	/* Check if file exists */
	ret = f_stat(FILENAME_SESSION_ID, &fno);

	/* if file doesnt exists */
	if(ret != FR_OK)
	{
		xil_printf("%s,file %s doesnt exist, creating now\n\r",__func__,FILENAME_SESSION_ID);

		/* Open a read position file for write operation */
		ret = f_open(&fil, FILENAME_SESSION_ID, FA_CREATE_ALWAYS | FA_WRITE);
		if (ret == FR_OK)
		{
			/* write  on flash */
			char buffWrite[3] = "0";
			unsigned int writecount = 0;

			if (f_write(&fil, buffWrite, strlen(buffWrite), &writecount) == FR_OK)
			{
				retVal = XST_SUCCESS;
			}

			/* Closing file */
			f_close(&fil);
		}
		else
		{
			xil_printf("%s,opening current position file for writing failed!, error %d\n\r",__func__,ret);
		}

		return retVal;
	}
	else
	{
		int sessionid = 0;

		/* Read Current Session id */
		ret = flashReadCurrentSessionID(&sessionid);
		if (ret == FR_OK)
		{
			sessionid++;
			ret = flashWriteSessionID(sessionid);
			if (ret != FR_OK)
			{
				xil_printf("%s,sessionid writing failed!, ret %d\n\r",__func__,ret);
			}
			else
			{
				retVal = XST_SUCCESS;
			}
		}
		else
		{
			xil_printf("%s,sessionid reading failed!, ret %d\n\r",__func__,ret);
		}
	}
	return retVal;
}

/**
 * @brief Read current session id saved on flash.
 *
 * This function reads session id saved on flash.
 */
int flashReadCurrentSessionID(int *sessionid)
{
	u8 retVal = XST_FAILURE;
	FIL fil;							/* File object */
	FRESULT ret;						/* Return variable for fatfs api return */

	/* Open a session id file for write and read operation */
	ret = f_open(&fil, FILENAME_SESSION_ID, FA_OPEN_ALWAYS | FA_READ);
	if (ret == FR_OK)
	{
		/* Read buffer from file */
		char buffRead[10] = "";
		unsigned int readcount = 0;
		if (f_read(&fil, buffRead, sizeof(buffRead), &readcount) == FR_OK)
		{
			/* update Sessionid */
			*sessionid = atoi(buffRead);

			retVal = XST_SUCCESS;
		}
		else
		{
			xil_printf("%s,reading sessionid failed!\n\r",__func__);
		}

		/* Closing file */
		f_close(&fil);
	}
	else
	{
		xil_printf("%s,opening sessionid file for updating failed!, ret %d\n\r",__func__,ret);
	}

	return retVal;
}

/**
 * @brief Write provided session id on flash.
 *
 * This function overwrites session id saved on flash.
 */
int flashWriteSessionID(int sessionid)
{
	u8 retVal = XST_FAILURE;
	FIL fil;							/* File object */
	FRESULT ret;						/* Return variable for fatfs api return */

	/* Open a session id file for write and read operation */
	ret = f_open(&fil, FILENAME_SESSION_ID, FA_CREATE_ALWAYS | FA_WRITE);
	if (ret == FR_OK)
	{
		/* save updated id on flash */
		char buffWrite[10] = "";
		unsigned int writecount = 0;

		itoa(sessionid,buffWrite,10);
		if (f_write(&fil, buffWrite, strlen(buffWrite), &writecount) == FR_OK)
		{
			retVal = XST_SUCCESS;
		}

		/* Closing file */
		f_close(&fil);
	}
	else
	{
		xil_printf("%s,opening sessionid file for writing failed!, error %d\n\r",__func__,ret);
	}

	return retVal;
}

/**
 * @brief Create directory for provided session id.
 *
 * This function creates a directory for provided session id.
 */
int flashCreateDirForSessionId(int sessionid)
{
	u8 retVal = XST_FAILURE;
	FRESULT ret;
	char dirPath[10] = "";

	/* Convert int to ascii */
	itoa(sessionid,dirPath,10);

	ret = f_mkdir(dirPath);
	if(ret == FR_OK)
	{
		retVal = XST_SUCCESS;
	}
	else
	{
		xil_printf("%s,creating directory for session id %d failed, error %d!\n\r",__func__,sessionid,ret);
	}

	return retVal;
}

/**
 * @brief Remove directory for provided session id.
 *
 * This function removes directory for provided session id.
 */
int flashRemoveDirForSessionId(int sessionid)
{
	u8 retVal = XST_FAILURE;
	FRESULT ret;
	char dirPath[64] = "";
	FILINFO fno;

	/* Convert int to ascii */
	itoa(sessionid,dirPath,10);

	/* Used custom function to create non empty dir as f_rmdir only deletes empty dir */
	ret = delete_node(dirPath, sizeof(dirPath), &fno);
	if(ret == FR_OK)
	{
		retVal = XST_SUCCESS;
	}
	else
	{
		xil_printf("%s,removed directory for session id %d failed, error %d!\n\r",__func__,sessionid,ret);
	}

	return retVal;
}

/**
 * @brief Check if directory for provided session id exists.
 *
 * This function checks if directory for provided session id.
 */
int flashIfDirForSessionIdExists(int sessionid)
{
	u8 retVal = XST_FAILURE;
	FRESULT ret;
	char dirPath[64] = "";
	FILINFO fno;

	/* Convert int to ascii */
	itoa(sessionid,dirPath,10);

	ret = f_stat(dirPath, &fno);
	if(ret == FR_OK)
	{
		retVal = XST_SUCCESS;
	}
	else
	{

	}

	return retVal;
}

/**
 * @brief Write data onto file on flash.
 *
 * This function appends data on msges on flash.
 */
int flashAddMsg(int filetype, int sessionid, char *msg)
{
	u8 retVal = XST_FAILURE;
	FIL fil;												/* File object */
	FRESULT ret = FR_INVALID_PARAMETER;						/* Return variable for fatfs api return */
	char filename[30] = "";

	if(!msg) {
		return retVal;
	}

	if(GetFileName(filetype,0,sessionid,filename) == XST_FAILURE)
	{
		xil_printf("%s,invalid file type, file type %d\n\r",__func__,filetype);
		return retVal;
	}

	ret = f_open(&fil, filename, FA_OPEN_APPEND | FA_WRITE);
	if (ret == FR_OK)
	{
		/* If Msg is not ending \n char, add it
		 * We are seperating msges with \n char.
		 * */
		/* append data on status file on flash */
		unsigned int writecount = 0;

		if (f_write(&fil, msg, strlen(msg), &writecount) == FR_OK)
		{
			if(writecount != strlen(msg))
			{
				xil_printf("%s,appending file failed!, file type %d, data to be written %d, data written %d\n\r",__func__,filetype,strlen(msg),writecount);
			}
			else
			{
				/* If MSG doesnt end with \n char, add it manually. */
				if(msg[strlen(msg)-1] != '\n')
				{
					char newline[]="\n";
					if (f_write(&fil, newline, strlen(newline), &writecount) != FR_OK)
					{
						xil_printf("%s,Writing \n char failed!, file type %d, error %d\n\r",__func__,filetype,ret);
					}
				}
				retVal = XST_SUCCESS;
			}
		}

		/* Closing file */
		f_close(&fil);
	}
	else
	{
		xil_printf("%s,opening file for appending failed!, file type %d, error %d\n\r",__func__,filetype,ret);
	}

	return retVal;
}

/**
 * @brief Read data from file on flash.
 *
 * This function reads and return data from flash.
 */
int flashReadMsg(int filetype, int sessionid, char *msg, int *readCount)
{
	u8 retVal = XST_FAILURE;
	FIL fil;												/* File object */
	FRESULT ret = FR_INVALID_PARAMETER;						/* Return variable for fatfs api return */
	char filename[30] = "";

	*readCount = 0;

	if(GetFileName(filetype,0,sessionid,filename) == XST_FAILURE)
	{
		xil_printf("%s,invalid file type, file type %d\n\r",__func__,filetype);
		return retVal;
	}

	ret = f_open(&fil, filename, FA_OPEN_ALWAYS | FA_READ);
	if (ret == FR_OK)
	{
		/* Offset file Read position */
		int ReadPositionValue = 0;
		flashGetCurrentReadPosition(filetype,sessionid,&ReadPositionValue);

		/* Check if file pointer is already at end */
		if(f_size(&fil) == ReadPositionValue)
		{
			*readCount = 0;

			/* if filetype is for general status notification delete file when all data sent */
			if((filetype == FILETYPE_GENERAL_STATUS) && (ReadPositionValue != 0))
			{
				/* Closing file */
				f_close(&fil);

				/* Used custom function to create non empty dir as f_rmdir only deletes empty dir */
				FILINFO fno;
				ret = f_unlink(filename);
				if(ret == FR_OK)
				{
					xil_printf("%s,removed file %s!\n\r",__func__,filename);
					flashWriteReadPosition(filetype,sessionid,0);
					xil_printf("%s,reseted read position file for %s!\n\r",__func__,filename);
					retVal = XST_SUCCESS;
				}
				else
				{
					xil_printf("%s,removed file %s for failed, error %d!\n\r",__func__,filename,ret);
				}
			}

			retVal = XST_SUCCESS;
		}
		else
		{
			f_lseek(&fil,ReadPositionValue);

			/* Read buffer from file */
			if (f_gets(msg, FILE_LINE_BUF_SIZE, &fil) != NULL)
			{
				*readCount = strlen(msg);
				flashWriteReadPosition(filetype,sessionid,f_tell(&fil));
				retVal = XST_SUCCESS;
			}
			else
			{
				xil_printf("%s,data read failed!\n\r",__func__);
			}

			/* Closing file */
			f_close(&fil);
		}
	}
	else
	{
		xil_printf("%s,opening file for reading failed!, file type %d, error %d\n\r",__func__,filetype,ret);
	}

	return retVal;
}

/**
 * @brief  offline control task for handling data saved during device offline.
 *
 * This function is a FreeRTOS task designed to manage the control processor operations, such as
 * executing control logic, processing commands, and interfacing with other system components.
 * The task is responsible for ensuring that the control processor's functions run smoothly within
 * the system's task scheduler.
 */
void prvOfflineControlTask( void *pvParameters )
{
	int readCount = 0;
	int ret = 0;

	while(true)
	{
		xil_printf("-------->%s,Printing Directory Structure Start!<--------\r\n",__func__);
		print_directory_structure("0:/", 0);
		xil_printf("-------->%s,Printing Directory Structure End!<--------\r\n",__func__);

#if 0
		/* Read and Print all available Data from RFID Log file for Debugging */
		xil_printf("-------->%s,RFIDLog File Data Start!<--------\r\n",__func__);
		{
			char *readBuffer = (char *) malloc(sizeof(char) * FILE_LINE_BUF_SIZE);
			do
			{
				if((flashReadMsg(FILETYPE_RFID_LOG,0,readBuffer,&readCount) == XST_SUCCESS) && (readCount))
				{
					/* Print Data */
					xil_printf("%s\r\n",readBuffer);
				}
				else
				{
					xil_printf("%s,No data available for RFIDLog file!\r\n",__func__);
				}

				memset(readBuffer,0,FILE_LINE_BUF_SIZE);

			} while (readCount);
			free(readBuffer);
		}
		flashWriteReadPosition(FILETYPE_RFID_LOG,0,0);
		xil_printf("-------->%s,RFIDLog File Data End!<--------\r\n",__func__);
#endif

		/* If Device is Online */
		if (g_networkConnectivity && g_isOCPPServerConnected)
		{
			/* Process data available on General Status notifications file (Without session) */
			/* Read and Send all available Data from Status file */
			do
			{
				char *readBuffer = (char *) malloc(sizeof(char) * FILE_LINE_BUF_SIZE);
				if((flashReadMsg(FILETYPE_GENERAL_STATUS,0,readBuffer,&readCount) == XST_SUCCESS) && (readCount))
				{
					/* Send Read Data to Cloud */
					ret = OCPPSendStatusNotificationMessage(MSG_TYPE_OFFLINE,NULL,readBuffer);
					if (ret == XST_SUCCESS)
					{
						xil_printf("%s, OCPP StatusNotification message sent successfully!\r\n",__func__);
					}
					else
					{
						xil_printf("%s, OCPP StatusNotification message sending failed!\r\n",__func__);
					}
				}
				else
				{
					xil_printf("%s,No data available for General StatusNotification message!\r\n",__func__);
					free(readBuffer);
				}
			} while (readCount);

			/* Start checking from 0, if dir for session exists */
			int CurrentSessionId = 0;
			ret = flashReadCurrentSessionID(&CurrentSessionId);
			if(ret != XST_SUCCESS)
			{
				xil_printf("%s,Current SessionId read failed, error %d\n\r",__func__,ret);
				return;
			}

			xil_printf("%s,Current SessionId %d\n\r",__func__,CurrentSessionId);

			int sessionid = (g_CurrentSessionId - OFFLINE_MODE_MAX_SESSIONS + 1);
			if(sessionid < 0) sessionid = 0;
			for(; sessionid <= CurrentSessionId; sessionid++)
			{
				/* Check if dir for session id exits */
				ret = flashIfDirForSessionIdExists(sessionid);
				if(ret != XST_SUCCESS)
				{
					//xil_printf("%s,Directory for SessionId Id %d doesn't exist\n\r",__func__,sessionid);
					continue;
				}

				/* Assign current session id in progress to global variable */
				g_OfflineSessionId = sessionid;

				/* Send Each file data */
				xil_printf("%s,Sending data for SessionId %d\n\r",__func__,sessionid);

				/* Commented as All status msg are saved in general file */
				// /* Read and Send all available Data from Status file */
				// do
				// {
				// 	char *readBuffer = (char *) malloc(sizeof(char) * FILE_LINE_BUF_SIZE);
				// 	if((flashReadMsg(FILETYPE_STATUS,sessionid,readBuffer,&readCount) == XST_SUCCESS) && (readCount))
				// 	{
				// 		/* Send Read Data to Cloud */
				// 		ret = OCPPSendStatusNotificationMessage(MSG_TYPE_OFFLINE,NULL,readBuffer);
				// 		if (ret == XST_SUCCESS)
				// 		{
				// 			xil_printf("%s,sessionid %d, OCPP StatusNotification message sent successfully!\r\n",__func__,sessionid);
				// 		}
				// 		else
				// 		{
				// 			xil_printf("%s,sessionid %d, OCPP StatusNotification message sending failed!\r\n",__func__,sessionid);
				// 			xil_printf("%s,Skipping sessionid %d!\r\n",__func__,sessionid);
				// 			goto SKIP;
				// 		}
				// 	}
				// 	else
				// 	{
				// 		xil_printf("%s,sessionid %d, No data available for OCPP StatusNotification message!\r\n",__func__,sessionid);
				// 		free(readBuffer);
				// 	}
				// } while (readCount);

				//xil_printf("%s,Data Sending from Status file completed for Session Id %d\n\r",__func__,sessionid);

				/* Read and Send all available Data from Transaction Start file */
				readCount = 0;
				do
				{
					char *readBuffer = (char *) malloc(sizeof(char) * FILE_LINE_BUF_SIZE);
					if((flashReadMsg(FILETYPE_TRANSACTION_START,sessionid,readBuffer,&readCount) == XST_SUCCESS) && (readCount))
					{
						/* Send Read Data to Cloud */
						ret = OCPPSendStartTransactionMessage(MSG_TYPE_OFFLINE,NULL,readBuffer);
						if (ret == XST_SUCCESS)
						{
							xil_printf("%s,sessionid %d, OCPP StartTransaction message sent successfully!\r\n",__func__,sessionid);
						}
						else
						{
							xil_printf("%s,sessionid %d, OCPP StartTransaction message sending failed!\r\n",__func__,sessionid);
							xil_printf("%s,sessionid %d, Skipping session!\r\n",__func__,sessionid);
							goto SKIP;
						}
					}
					else
					{
						xil_printf("%s,sessionid %d, No data available for OCPP StartTransaction message!\r\n",__func__,sessionid);
						free(readBuffer);
					}
				} while (readCount);

				//xil_printf("%s,Data Sending from Transaction file completed for Session Id %d\n\r",__func__,sessionid);

				/* Read and Send all available Data from Transaction Stop file */
				readCount = 0;
				do
				{
					char *readBuffer = (char *) malloc(sizeof(char) * FILE_LINE_BUF_SIZE);
					if((flashReadMsg(FILETYPE_TRANSACTION_STOP,sessionid,readBuffer,&readCount) == XST_SUCCESS) && (readCount))
					{
						/* Send Read Data to Cloud */
						ret = OCPPSendStopTransactionMessage(MSG_TYPE_OFFLINE,NULL,readBuffer);
						if (ret == XST_SUCCESS)
						{
							xil_printf("%s,sessionid %d, OCPP StopTransaction message sent successfully!\r\n",__func__, sessionid);
						}
						else
						{
							xil_printf("%s,sessionid %d, OCPP StopTransaction message sending failed!\r\n",__func__, sessionid);
							xil_printf("%s,sessionid %d, Skipping session!\r\n",__func__,sessionid);
							goto SKIP;
						}
					}
					else
					{
						xil_printf("%s,sessionid %d, No data available for OCPP StopTransaction message!\r\n",__func__, sessionid);
						free(readBuffer);
					}
				} while (readCount);

				/* Read and Send all available Data from Meter Progress Update Values file */
				readCount = 0;
				do
				{
					char *readBuffer1 = (char *) malloc(sizeof(char) * FILE_LINE_BUF_SIZE);
					char *readBuffer2 = (char *) malloc(sizeof(char) * FILE_LINE_BUF_SIZE);
					char *readBuffer3 = (char *) malloc(sizeof(char) * FILE_LINE_BUF_SIZE);
					if((flashReadMsg(FILETYPE_METERVAL_PROG_UPDATE,sessionid,readBuffer1,&readCount) == XST_SUCCESS) && (readCount))
					{
						ret = flashReadMsg(FILETYPE_METERVAL_PROG_UPDATE,sessionid,readBuffer2,&readCount);
						ret = flashReadMsg(FILETYPE_METERVAL_PROG_UPDATE,sessionid,readBuffer3,&readCount);
						if (ret != XST_SUCCESS)
						{
							xil_printf("%s,sessionid %d, OCPP MeterValues Prog Update message reading failed!\r\n",__func__, sessionid);
							xil_printf("%s,sessionid %d, Skipping session!\r\n",__func__,sessionid);
							goto SKIP;
						}

						/* Send Read Data to Cloud */
						ret = OCPPSendMeterValuesMessage(MSG_TYPE_OFFLINE,METER_VALUES_PROGRESS_UPDATE,NULL,readBuffer1,readBuffer2,readBuffer3);
						if (ret == XST_SUCCESS)
						{
							xil_printf("%s,sessionid %d, OCPP MeterValues Prog Update message sent successfully!\r\n",__func__, sessionid);
						}
						else
						{
							xil_printf("%s,sessionid %d, OCPP MeterValues Prog Update message sending failed!\r\n",__func__, sessionid);
							xil_printf("%s,sessionid %d, Skipping session!\r\n",__func__,sessionid);
							goto SKIP;
						}
					}
					else
					{
						xil_printf("%s,sessionid %d, No data available for OCPP MeterValues Prog Update message!\r\n",__func__, sessionid);
						free(readBuffer1);
						free(readBuffer2);
						free(readBuffer3);
					}
				} while (readCount);

				/* Read and Send all available Data from Meter Summary Update Values file */
				readCount = 0;
				do
				{
					char *readBuffer1 = (char *) malloc(sizeof(char) * FILE_LINE_BUF_SIZE);
					char *readBuffer2 = (char *) malloc(sizeof(char) * FILE_LINE_BUF_SIZE);
					if((flashReadMsg(FILETYPE_METERVAL_SUMMARY_UPDATE,sessionid,readBuffer1,&readCount) == XST_SUCCESS) && (readCount))
					{
						ret = flashReadMsg(FILETYPE_METERVAL_SUMMARY_UPDATE,sessionid,readBuffer2,&readCount);
						if (ret != XST_SUCCESS)
						{
							xil_printf("%s,sessionid %d, OCPP MeterValues Prog Update message reading failed!\r\n",__func__, sessionid);
							xil_printf("%s,sessionid %d, Skipping session!\r\n",__func__,sessionid);
							goto SKIP;
						}

						/* Send Read Data to Cloud */
						ret = OCPPSendMeterValuesMessage(MSG_TYPE_OFFLINE,METER_VALUES_SUMMARY_UPDATE,NULL,readBuffer1,readBuffer2,NULL);
						if (ret == XST_SUCCESS)
						{
							xil_printf("%s,sessionid %d, OCPP MeterValues Summary Update message sent successfully!\r\n",__func__, sessionid);
						}
						else
						{
							xil_printf("%s,sessionid %d, OCPP MeterValues Summary Update message sending failed!\r\n",__func__, sessionid);
							xil_printf("%s,sessionid %d, Skipping session!\r\n",__func__,sessionid);
							goto SKIP;
						}
					}
					else
					{
						xil_printf("%s,sessionid %d, No data available for OCPP MeterValues Summary Update message!\r\n",__func__, sessionid);
						free(readBuffer1);
						free(readBuffer2);
					}
				} while (readCount);

				//xil_printf("%s,Data Sending from Meter Values file completed for Session Id %d\n\r",__func__,sessionid);

				/* All data for sessions sent reset session ids */
				if(sessionid != CurrentSessionId)
				{
					/* Delete dir as all data sent for session */
					ret = flashRemoveDirForSessionId(sessionid);
					if(ret != XST_SUCCESS)
					{
						xil_printf("%s,Dir remove failed for sessionid, error %d\n\r",__func__,ret);
					}
					else
					{
						xil_printf("%s,Directory removed for Session Id %d\n\r",__func__,sessionid);
					}
				}
				/* if this is last session and session is completed */
				else if(g_EVSECurrentState == EVSE_STATE_USER_AUTHENTICATION_PENDING)
				{
					/* Delete dir as all data sent for session */
					ret = flashRemoveDirForSessionId(sessionid);
					if(ret != XST_SUCCESS)
					{
						xil_printf("%s,Dir remove failed for sessionid, error %d\n\r",__func__,ret);
					}
					else
					{
						xil_printf("%s,Directory removed for Session Id %d\n\r",__func__,sessionid);
					}

					/* Reset session id saved on flash to 0 */
					g_CurrentSessionId = 0;
					ret = flashWriteSessionID(g_CurrentSessionId);
					if(ret == XST_FAILURE)
					{
						xil_printf("%s, Reseting Session Id to 0 failed, Error : %d\r\n", __func__, ret);
					}

					ret = flashReadCurrentSessionID(&g_CurrentSessionId);
					if(ret == XST_FAILURE)
					{
						xil_printf("%s, Session Id read failed, Error : %d\r\n", __func__, ret);
					}

					xil_printf("%s, Reseted Session Id to %d\r\n", __func__,g_CurrentSessionId);

					// Wait for the LCD mutex
					if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE)
					{
						/* Print the details on the LCD module */
						lcdPrintOfflineSessionSyncedMessage();

						// Release the mutex
						xSemaphoreGive(xLCDMutex);
					}
				}

				// SKIP:
				// 	vTaskDelay(INT_TO_TICKS(100));
			}
		}

	SKIP:
		vTaskDelay(INT_TO_TICKS(2000));
	}
}
