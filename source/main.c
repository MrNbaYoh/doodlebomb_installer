#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <3ds.h>
#include "crc.h"

#define LETTER_LIST_OFFSET 0x5A0
#define LETTER_ENTRY_LENGTH 0x80
#define ENTRIES_CHUNK_SIZE 0x141E40
#define CHK_OFFSET 0x70

Result GetMyPrincipalId(Handle handle, u32* out)
{
	u32 *cmdbuf = getThreadCommandBuffer();
	cmdbuf[0] = 0x00050000; //GetMyFriendKey

	Result ret = 0;
	if(R_FAILED(ret = svcSendSyncRequest(handle))) return ret;

	*out = cmdbuf[2];
	return cmdbuf[1];
}

Result GetMyScreenName(Handle handle, wchar_t *out)
{
	u32 *cmdbuf = getThreadCommandBuffer();
	cmdbuf[0] = 0x00090000;

	Result ret = 0;
	if(R_FAILED(ret = svcSendSyncRequest(handle))) return ret;

	wcscpy(out, (wchar_t*)&cmdbuf[2]);
	return cmdbuf[1];
}

u64 swap_u64(u64 val)
{
    val = ((val << 8) & 0xFF00FF00FF00FF00ULL ) | ((val >> 8) & 0x00FF00FF00FF00FFULL );
    val = ((val << 16) & 0xFFFF0000FFFF0000ULL ) | ((val >> 16) & 0x0000FFFF0000FFFFULL );
    return (val << 32) | (val >> 32);
}

Result getEntryIndex(FS_Archive archive, u32 *index)
{
	u64 *buf;
	u32 nb;
	u32 *buffer = 0;
	Handle manageFile = 0;
	Result ret = FSUSER_OpenFile(&manageFile, archive, fsMakePath(PATH_ASCII, "/letter/manage.bin"), FS_OPEN_WRITE|FS_OPEN_READ, 0);
	if(ret)
	{
		printf("An error occured while opening manage.bin : %08lX\n", ret);
		goto end;
	}

	u64 size = 0;
	ret = FSFILE_GetSize(manageFile, &size);
	if(ret)
	{
		printf("Failed to get manage.bin size : %08lX\n", ret);
		goto end;
	}

	buffer = malloc(size);
	u32 bytesRead = 0;
	ret = FSFILE_Read(manageFile, &bytesRead, 0, (u8*)buffer, (u32)size);
	if(ret)
	{
		printf("Failed to read manage.bin : %08lX\n", ret);
		goto end;
	}

	end:
	if(!ret)
	{
		buf = (u64*)&buffer[LETTER_LIST_OFFSET/sizeof(u32)];
		nb = buf[0];
		if(nb == 0)
		{
			printf("No letter found! Please create a letter before trying to install doodlebomb!\n");
			ret = -1;
			goto error;
		}

		u64 max = 0;
		u32 id = 0;

		for(int i = 0; i < nb; i++)
		{
			u64 current = swap_u64(buf[0x8 + i*0x10 + 0x2]);
			//printf("%16llX\n", current);
			if(current > max)
			{
				max = current;
				id = (u32)buf[0x8 + i*0x10 + 0x8];
				//printf("%08lX\n", id);
			}
		}

		*index = id;
	}

	error:
	if(buffer) free(buffer);
	FSFILE_Close(manageFile);
	return ret;
}

Result copyLetter(FS_Archive archive, u32 index, char region[4])
{
	Result ret = 0;
	u8 *buffer = 0;
	Handle fileHandle = 0;
	char romfsPath[15] = "romfs:/";
	strcat(romfsPath, region);
	strcat(romfsPath, ".bin");
	FILE *letter = fopen(romfsPath, "rb");

	if(!letter)
	{
		printf("An error occured while opening the letter file in romfs!");
		ret = -1;
		goto end;
	}

	fseek(letter, 0, SEEK_END);
	u32 size = ftell(letter);
	fseek(letter, 0, SEEK_SET);

	buffer = malloc(size);
	fread(buffer, size, 1, letter);

	char filename[24] = {0};
	sprintf(filename, "/letter/0000/lt%04lu.bin", index);
	FS_Path path = fsMakePath(PATH_ASCII, filename);
	ret = FSUSER_DeleteFile(archive, path);
	if(ret)
	{
		printf("Failed to delete letter in extdata:/letter/0000/ : %08lX\n", ret);
		goto end;
	}

	ret = FSUSER_CreateFile(archive, path, 0, size);
	if(ret)
	{
		printf("Failed to create letter in extdata:/letter/0000/ : %08lX\n", ret);
		goto end;
	}

	ret = FSUSER_OpenFile(&fileHandle, archive, path, FS_OPEN_WRITE, 0);
	if(ret)
	{
		printf("Failed to open letter in extdata:/letter/0000/ : %08lX\n", ret);
		goto end;
	}

	u32 written = 0;
	ret = FSFILE_Write(fileHandle, &written, 0, buffer, size, FS_WRITE_FLUSH);
	if(ret)
		printf("Failed to write to extdata:/letter/0000/ : %08lX\n", ret);

	end:
	if(buffer) free(buffer);
	FSFILE_Close(fileHandle);
	return ret;
}

void install(char* region, u32 id)
{
	fsInit();
	Result rc = romfsInit();
	if (rc)
		printf("romfsInit: %08lX\n", rc);
	else
	{
		FS_Archive arch = 0;
		u32 path[3] = {MEDIATYPE_SD, id, 0};
		rc = FSUSER_OpenArchive(&arch, ARCHIVE_EXTDATA, (FS_Path){PATH_BINARY, 0xC, path});
		if(!rc)
		{
			u32 index = 0;
			rc = getEntryIndex(arch, &index);
			printf("INDEX %lu\n", index);
			if(!rc)
			{
				rc = copyLetter(arch, index, region);
			}
			FSUSER_CloseArchive(arch);
		}
		else
		{
			printf("Failed to open ExtData, are you sure you started the game at least once and/or chose the right region?\n");
		}
	}

	if(!rc)
		printf("Doodlebomb has been successfully installed !\n");

	printf("Press START to exit.\n");
	romfsExit();
	fsExit();
}


int main()
{
	gfxInitDefault();
	consoleInit(GFX_TOP, NULL);

	bool done = false;

	printf("Press a button according to your region : (A)EUR, (X)USA, (Y)JAP\n");
	// Main loop
	while (aptMainLoop())
	{
		gspWaitForVBlank();
		hidScanInput();

		u32 kDown = hidKeysDown();
		if (!done && kDown & KEY_A)
		{
		 	install("EUR", 0x1A2E);
			done = true;
		}
		else if (!done && kDown & KEY_X)
		{
			install("USA", 0x1A2D);
			done = true;
		}
		else if	(!done && kDown & KEY_Y)
		{
			install("JAP", 0x1A2C);
			done = true;
		}
		if (kDown & KEY_START)
			break; // break in order to return to hbmenu
	}

	gfxExit();
	return 0;
}
