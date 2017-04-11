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

Result getEntryIndex(FS_Archive archive, u32 *index)
{
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
		u32 *buf = &buffer[LETTER_LIST_OFFSET/sizeof(u32)];
		u32 nb = buf[0];
		u32 ids[nb];
		memset(ids, 0, nb*sizeof(u32));
		u32 max = 0;
		for(int i = 0; i < nb; i++)
		{
			ids[i] = buf[0x20 + i*0x20];
			if(max < ids[i])
				max = ids[i];
		}

		bool present[max+1];
		memset(present, false, (max+1)*sizeof(bool));
		for(int i = 0; i < nb; i++)
			present[ids[i]] = true;

		int i;
		for(i = 0; i < max+1 && present[i]; i++);

		*index = i;
	}

	if(buffer) free(buffer);
	FSFILE_Close(manageFile);
	return ret;
}

Result addLetterEntry(FS_Archive archive, u32 index)
{
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

	Handle frduHandle = 0;
	ret = srvGetServiceHandle(&frduHandle, "frd:u");
	if(ret)
	{
		printf("Failed to get frd:u handle : %08lX\n", ret);
		goto end;
	}

	u32 id = 0;
	ret = GetMyPrincipalId(frduHandle, &id);

	if(ret)
	{
		printf("Failed to get friend key : %08lX\n", ret);
		svcCloseHandle(frduHandle);
		goto end;
	}

	u32 nbEntries = buffer[LETTER_LIST_OFFSET/sizeof(u32)];

	u32 entry[LETTER_ENTRY_LENGTH/sizeof(u32)] = {0};
	memcpy(&entry[0x0/sizeof(u32)], "DOODBOMB", 0x8);
	memset(&entry[0x10/sizeof(u32)], 0xFF, 0x8);
	entry[0x18/sizeof(u32)] = id;
	entry[0x24/sizeof(u32)] = 0x00010202;
	entry[0x40/sizeof(u32)] = index;
	entry[0x44/sizeof(u32)]= 0xFFFFFFFF;
	ret = GetMyScreenName(frduHandle, (wchar_t*)&entry[0x28/sizeof(u32)]);
	svcCloseHandle(frduHandle);

	if(ret)
	{
		printf("Failed to get screen name : %08lX\n", ret);
		goto end;
	}

	buffer[LETTER_LIST_OFFSET/sizeof(u32)] = nbEntries+1;
	memcpy(&buffer[(LETTER_LIST_OFFSET + 0x40 + LETTER_ENTRY_LENGTH*nbEntries)/sizeof(u32)], entry, LETTER_ENTRY_LENGTH);

	u32 checksum = crc32((unsigned char*)&buffer[LETTER_LIST_OFFSET/sizeof(u32)], ENTRIES_CHUNK_SIZE);
	buffer[CHK_OFFSET/sizeof(u32)] = checksum;

	u32 written = 0;
	ret = FSFILE_Write(manageFile, &written, 0, buffer, size, FS_WRITE_FLUSH);
	if(ret)
		printf("Failed to write to manage.bin : %08lX\n", ret);

	end:
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

Result getRegionStr(u64 id, char *out)
{
	switch(id)
	{
		case 0x00040000001A2E00:
			strcpy(out, "EUR");
			break;
		case 0x00040000001A2D00:
			strcpy(out, "USA");
			break;
		case 0x00040000001A2C00:
			strcpy(out, "JAP");
			break;
		default:
			printf("Please launch this installer with Swapdoodle...\n");
			return -1;
	}
	return 0;
}

int main()
{
	gfxInitDefault();
	consoleInit(GFX_TOP, NULL);

	u64 id = 0;
	APT_GetProgramID(&id);

	fsInit();
	Result rc = romfsInit();
	if (rc)
		printf("romfsInit: %08lX\n", rc);
	else
	{
		FS_Archive arch = 0;
		u32 path[3] = {MEDIATYPE_SD, (id>>8)&0xFFFF, 0};
		rc = FSUSER_OpenArchive(&arch, ARCHIVE_EXTDATA, (FS_Path){PATH_BINARY, 0xC, path});
		if(!rc)
		{
			char region[4];
			rc = getRegionStr(id, region);
			if(!rc)
			{
				u32 index = 0;
				rc = getEntryIndex(arch, &index);
				printf("INDEX %lu\n", index);
				if(!rc)
				{
					rc = copyLetter(arch, index, region);
					if(!rc)
						rc = addLetterEntry(arch, index);
				}
			}
		}
		FSUSER_CloseArchive(arch);
	}

	if(!rc)
		printf("Doodlebomb has been successfully installed !\n");

	printf("Press START to exit.\n");
	// Main loop
	while (aptMainLoop())
	{
		gspWaitForVBlank();
		hidScanInput();

		u32 kDown = hidKeysDown();
		if (kDown & KEY_START)
			break; // break in order to return to hbmenu
	}

	romfsExit();
	fsExit();
	gfxExit();
	return 0;
}
