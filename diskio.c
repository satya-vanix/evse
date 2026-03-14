/*-----------------------------------------------------------------------*/
/* Low level disk I/O module SKELETON for FatFs     (C)ChaN, 2019        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include "ff.h"			/* Obtains integer types */
#include "diskio.h"		/* Declarations of disk functions */

#include "common.h"
#include "qspiControl.h"

/* Definitions of physical drive number for each drive */
#define DEV_QSPI_FLASH	0	/* Map QSPI flash to physical drive 0 */

extern XQspiPs g_qspiInstance;		/* Global variable to store the QSPI flash instance handler */
static volatile DSTATUS Stat = STA_NOINIT;	/* Disk status */

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (
	BYTE pdrv		/* Physical drive nmuber to identify the drive */
)
{
	if(pdrv != DEV_QSPI_FLASH)
		return STA_NOINIT;

	return Stat;
}



/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
	BYTE pdrv				/* Physical drive nmuber to identify the drive */
)
{
	Stat = STA_NOINIT;

	switch (pdrv)
	{
		case DEV_QSPI_FLASH :
			if(&g_qspiInstance != NULL)
			{
				Stat = RES_OK;
			}
			break;
	}

	return Stat;
}



/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read (
	BYTE pdrv,		/* Physical drive nmuber to identify the drive */
	BYTE *buff,		/* Data buffer to store read data */
	LBA_t sector,	/* Start sector in LBA */
	UINT count		/* Number of sectors to read */
)
{
	DRESULT res = RES_ERROR;

	switch (pdrv) {
	case DEV_QSPI_FLASH :
		// translate the arguments here
		int ret = qspiRead(&g_qspiInstance, buff, ((sector + QSPI_FATFS_OFFSET_SECTOR_COUNT) * QSPI_SECTOR_SIZE), (count * QSPI_SECTOR_SIZE));
		if(ret == QSPI_SUCCESS)
		{
			res = RES_OK;
		}
		return res;
	}

	return RES_PARERR;
}



/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if FF_FS_READONLY == 0

DRESULT disk_write (
	BYTE pdrv,			/* Physical drive nmuber to identify the drive */
	const BYTE *buff,	/* Data to be written */
	LBA_t sector,		/* Start sector in LBA */
	UINT count			/* Number of sectors to write */
)
{
	DRESULT res = RES_ERROR;

	switch (pdrv) {
		case DEV_QSPI_FLASH :
		// translate the arguments here
		int ret = qspiWrite(&g_qspiInstance, buff, ((sector + QSPI_FATFS_OFFSET_SECTOR_COUNT) * QSPI_SECTOR_SIZE), (count * QSPI_SECTOR_SIZE));
		if(ret == QSPI_SUCCESS)
		{
			res = RES_OK;
		}
		return res;
	}

	return RES_PARERR;
}

#endif


/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl (
	BYTE pdrv,		/* Physical drive nmuber (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
	DRESULT res = RES_OK;

	switch (pdrv) {
		case DEV_QSPI_FLASH :
			switch(cmd)
			{
				case CTRL_SYNC :
					break;

				case GET_BLOCK_SIZE:
					*(DWORD*)buff = QSPI_BLOCK_SIZE;
					break;


				case GET_SECTOR_SIZE:
					*(DWORD*)buff = QSPI_SECTOR_SIZE;
					break;

				case GET_SECTOR_COUNT:
					*(DWORD*)buff = QSPI_FATFS_OFFSET_SECTOR_COUNT;
					break;

				default:
					break;
			}
	}

	return res;
}

