/**
 * Kinesis Video Producer DeviceInfo
 */
#define LOG_CLASS "DeviceInfoProvider"
#include "Include_i.h"

STATUS createDefaultDeviceInfo(PDeviceInfo* ppDeviceInfo)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PDeviceInfo pDeviceInfo = NULL;
    PCHAR userLogLevelStr = NULL;
    UINT32 userLogLevel;

    CHK(ppDeviceInfo != NULL, STATUS_NULL_ARG);

    // allocate the entire struct
    pDeviceInfo = MEMCALLOC(1, SIZEOF(DeviceInfo));
    CHK(pDeviceInfo != NULL, STATUS_NOT_ENOUGH_MEMORY);
    pDeviceInfo->version = DEVICE_INFO_CURRENT_VERSION;
    pDeviceInfo->name[0] = '\0';
    pDeviceInfo->tagCount = 0;
    pDeviceInfo->tags = NULL;
    pDeviceInfo->streamCount = DEFAULT_STREAM_COUNT;

    pDeviceInfo->storageInfo.version = STORAGE_INFO_CURRENT_VERSION;
    pDeviceInfo->storageInfo.storageType = DEVICE_STORAGE_TYPE_IN_MEM;
    pDeviceInfo->storageInfo.storageSize = DEFAULT_DEVICE_STORAGE_SIZE;
    pDeviceInfo->storageInfo.rootDirectory[0] = '\0';
    pDeviceInfo->storageInfo.spillRatio = 0;

    pDeviceInfo->clientId[0] = '\0';
    pDeviceInfo->clientInfo.version = CLIENT_INFO_CURRENT_VERSION;
    pDeviceInfo->clientInfo.loggerLogLevel = DEFAULT_LOGGER_LOG_LEVEL;
    if ((userLogLevelStr = GETENV(DEBUG_LOG_LEVEL_ENV_VAR)) != NULL) {
        if (STATUS_SUCCEEDED(STRTOUI32(userLogLevelStr, NULL, 10, &userLogLevel))) {
            userLogLevel = userLogLevel > LOG_LEVEL_SILENT ? LOG_LEVEL_SILENT : userLogLevel < LOG_LEVEL_VERBOSE ? LOG_LEVEL_VERBOSE : userLogLevel;
            pDeviceInfo->clientInfo.loggerLogLevel = userLogLevel;
        } else {
            DLOGW("failed to parse %s", DEBUG_LOG_LEVEL_ENV_VAR);
        }
    }

    pDeviceInfo->clientInfo.logMetric = TRUE;

    // use 0 for default values
    pDeviceInfo->clientInfo.stopStreamTimeout = 0;
    pDeviceInfo->clientInfo.createClientTimeout = 0;
    pDeviceInfo->clientInfo.createStreamTimeout = 0;

CleanUp:

    if (!STATUS_SUCCEEDED(retStatus)) {
        freeDeviceInfo(&pDeviceInfo);
        pDeviceInfo = NULL;
    }

    if (pDeviceInfo != NULL) {
        *ppDeviceInfo = pDeviceInfo;
    }

    LEAVES();
    return retStatus;
}

STATUS setDeviceInfoStorageSize(PDeviceInfo pDeviceInfo, UINT64 storageSize)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;

    CHK(pDeviceInfo != NULL, STATUS_NULL_ARG);
    CHK(storageSize <= MAX_STORAGE_ALLOCATION_SIZE, STATUS_INVALID_STORAGE_SIZE);

    pDeviceInfo->storageInfo.storageSize = storageSize;

CleanUp:

    LEAVES();
    return retStatus;
}

STATUS setDeviceInfoStorageSizeBasedOnBitrateAndBufferDuration(PDeviceInfo pDeviceInfo,
                                                               UINT64 averageBitsPerSecond, UINT64 bufferDuration)
{
    ENTERS();
    UINT64 estimatedStorageSize = 0;
    STATUS retStatus = STATUS_SUCCESS;

    CHK(pDeviceInfo != NULL, STATUS_NULL_ARG);
    CHK(averageBitsPerSecond > 0 && bufferDuration > 0, STATUS_INVALID_ARG);

    estimatedStorageSize = (UINT64) (pDeviceInfo->streamCount *
                           (((DOUBLE) averageBitsPerSecond / 8) * ((DOUBLE) bufferDuration / HUNDREDS_OF_NANOS_IN_A_SECOND)) *
                           STORAGE_ALLOCATION_DEFRAGMENTATION_FACTOR);

    CHK_STATUS(setDeviceInfoStorageSize(pDeviceInfo, estimatedStorageSize));

CleanUp:

    LEAVES();
    return retStatus;
}

STATUS setDeviceInfoFromConfigFile(PDeviceInfo pDeviceConfigFromFile, PCHAR filePath) {
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    CHK(pDeviceConfigFromFile != NULL && filePath != NULL, STATUS_NULL_ARG);
    UINT64 fileSize = 0;
    BYTE params[1024];
    CHAR frameFilePath[MAX_PATH_LEN + 1];

    jsmn_parser parser;
    jsmn_init(&parser);
    jsmntok_t tokens[256];
    INT64 tokenCount;
    CHAR finalAttr[256];

    MEMSET(frameFilePath, 0x00, MAX_PATH_LEN + 1);
    STRCPY(frameFilePath, filePath);

    CHK_STATUS(readFile(frameFilePath, TRUE, NULL, &fileSize));
    CHK_STATUS(readFile(frameFilePath, TRUE, params, &fileSize));

    tokenCount = jsmn_parse(&parser, (PCHAR)params, fileSize, tokens, SIZEOF(tokens) / SIZEOF(jsmntok_t));
    if(tokenCount < 0) {
        DLOGE("JSMN Parse failed with error %d", tokenCount);
        retStatus = STATUS_INTERNAL_ERROR;
    }
    else {
        for (UINT32 i = 1; i < (UINT32) tokenCount; i++) {
            if(compareJsonString((PCHAR)params, &tokens[i], JSMN_STRING, (PCHAR) "DEFAULT_DEVICE_STORAGE_TYPE")) {
                CHK_STATUS(getParsedValue(params, tokens[i+1], finalAttr));
                if (STRCMP(finalAttr, "HYBRID_FILE") == 0) {
                    pDeviceConfigFromFile->storageInfo.storageType = DEVICE_STORAGE_TYPE_HYBRID_FILE;
                }
                else if(STRCMP(finalAttr, "CONTENT_STORE") == 0) {
                    pDeviceConfigFromFile->storageInfo.storageType = DEVICE_STORAGE_TYPE_IN_MEM_CONTENT_STORE_ALLOC;
                }
                else {
                    pDeviceConfigFromFile->storageInfo.storageType = DEVICE_STORAGE_TYPE_IN_MEM;
                }
                DLOGD("Storage type parsed: %s (type %d)", finalAttr, pDeviceConfigFromFile->storageInfo.storageType);
                i++;
            }
            if(compareJsonString((PCHAR)params, &tokens[i], JSMN_STRING, (PCHAR) "DEFAULT_DEVICE_STORAGE_SIZE")) {
                CHK_STATUS(getParsedValue(params, tokens[i+1], finalAttr));
                STRTOUI64(finalAttr, NULL, 10, &pDeviceConfigFromFile->storageInfo.storageSize);
                DLOGD("Storage type parsed: %lu bytes", pDeviceConfigFromFile->storageInfo.storageSize);
                i++;
            }
        }
    }

CleanUp:
    LEAVES();
    return retStatus;
}

STATUS freeDeviceInfo(PDeviceInfo* ppDeviceInfo)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;

    CHK(ppDeviceInfo != NULL, STATUS_NULL_ARG);
    CHK(*ppDeviceInfo != NULL, retStatus);

    MEMFREE(*ppDeviceInfo);
    *ppDeviceInfo = NULL;

CleanUp:

    LEAVES();
    return retStatus;
}
