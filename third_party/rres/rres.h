/**********************************************************************************************
*
*   rres v1.0 - A simple and easy-to-use file-format to package resources
*
*   CONFIGURATION:
*
*   #define RRES_IMPLEMENTATION
*       Generates the implementation of the library into the included file
*       If not defined, the library is in header only mode and can be included in other headers
*       or source files without problems. But only ONE file should hold the implementation
*
*   LICENSE: MIT (c) 2016-2026 Ramon Santamaria (@raysan5)
*
**********************************************************************************************/

#ifndef RRES_H
#define RRES_H

#if defined(_WIN32)
    #if defined(BUILD_LIBTYPE_SHARED)
        #define RRESAPI __declspec(dllexport)
    #elif defined(USE_LIBTYPE_SHARED)
        #define RRESAPI __declspec(dllimport)
    #endif
#endif

#ifndef RRESAPI
    #define RRESAPI
#endif

#ifndef RRES_MALLOC
    #define RRES_MALLOC(sz)         malloc(sz)
#endif
#ifndef RRES_CALLOC
    #define RRES_CALLOC(ptr,sz)     calloc(ptr,sz)
#endif
#ifndef RRES_REALLOC
    #define RRES_REALLOC(ptr,sz)    realloc(ptr,sz)
#endif
#ifndef RRES_FREE
    #define RRES_FREE(ptr)          free(ptr)
#endif

#define RRES_SUPPORT_LOG_INFO
#if defined(RRES_SUPPORT_LOG_INFO)
    #define RRES_LOG(...) printf(__VA_ARGS__)
#else
    #define RRES_LOG(...)
#endif

#define RRES_MAX_FILENAME_SIZE      1024

typedef struct rresFileHeader {
    unsigned char id[4];
    unsigned short version;
    unsigned short chunkCount;
    unsigned int cdOffset;
    unsigned int reserved;
} rresFileHeader;

typedef struct rresResourceChunkInfo {
    unsigned char type[4];
    unsigned int id;
    unsigned char compType;
    unsigned char cipherType;
    unsigned short flags;
    unsigned int packedSize;
    unsigned int baseSize;
    unsigned int nextOffset;
    unsigned int reserved;
    unsigned int crc32;
} rresResourceChunkInfo;

typedef struct rresResourceChunkData {
    unsigned int propCount;
    unsigned int *props;
    void *raw;
} rresResourceChunkData;

typedef struct rresResourceChunk {
    rresResourceChunkInfo info;
    rresResourceChunkData data;
} rresResourceChunk;

typedef struct rresResourceMulti {
    unsigned int count;
    rresResourceChunk *chunks;
} rresResourceMulti;

typedef struct rresDirEntry {
    unsigned int id;
    unsigned int offset;
    unsigned int reserved;
    unsigned int fileNameSize;
    char fileName[RRES_MAX_FILENAME_SIZE];
} rresDirEntry;

typedef struct rresCentralDir {
    unsigned int count;
    rresDirEntry *entries;
} rresCentralDir;

typedef struct rresFontGlyphInfo {
    int x;
    int y;
    int width;
    int height;
    int value;
    int offsetX;
    int offsetY;
    int advanceX;
} rresFontGlyphInfo;

typedef enum rresResourceDataType {
    RRES_DATA_NULL         = 0,
    RRES_DATA_RAW          = 1,
    RRES_DATA_TEXT         = 2,
    RRES_DATA_IMAGE        = 3,
    RRES_DATA_WAVE         = 4,
    RRES_DATA_VERTEX       = 5,
    RRES_DATA_FONT_GLYPHS  = 6,
    RRES_DATA_LINK         = 99,
    RRES_DATA_DIRECTORY    = 100,
} rresResourceDataType;

typedef enum rresCompressionType {
    RRES_COMP_NONE          = 0,
    RRES_COMP_RLE           = 1,
    RRES_COMP_DEFLATE       = 10,
    RRES_COMP_LZ4           = 20,
    RRES_COMP_LZMA2         = 30,
    RRES_COMP_QOI           = 40,
} rresCompressionType;

typedef enum rresEncryptionType {
    RRES_CIPHER_NONE        = 0,
    RRES_CIPHER_XOR         = 1,
    RRES_CIPHER_DES         = 10,
    RRES_CIPHER_TDES        = 11,
    RRES_CIPHER_IDEA        = 20,
    RRES_CIPHER_AES         = 30,
    RRES_CIPHER_AES_GCM     = 31,
    RRES_CIPHER_XTEA        = 40,
    RRES_CIPHER_BLOWFISH    = 50,
    RRES_CIPHER_RSA         = 60,
    RRES_CIPHER_SALSA20     = 70,
    RRES_CIPHER_CHACHA20    = 71,
    RRES_CIPHER_XCHACHA20   = 72,
    RRES_CIPHER_XCHACHA20_POLY1305 = 73,
} rresEncryptionType;

typedef enum rresErrorType {
    RRES_SUCCESS = 0,
    RRES_ERROR_FILE_NOT_FOUND,
    RRES_ERROR_FILE_FORMAT,
    RRES_ERROR_MEMORY_ALLOC,
} rresErrorType;

typedef enum rresTextEncoding {
    RRES_TEXT_ENCODING_UNDEFINED = 0,
    RRES_TEXT_ENCODING_UTF8      = 1,
    RRES_TEXT_ENCODING_UTF8_BOM  = 2,
    RRES_TEXT_ENCODING_UTF16_LE  = 10,
    RRES_TEXT_ENCODING_UTF16_BE  = 11,
} rresTextEncoding;

typedef enum rresCodeLang {
    RRES_CODE_LANG_UNDEFINED = 0,
    RRES_CODE_LANG_C,
    RRES_CODE_LANG_CPP,
    RRES_CODE_LANG_CS,
    RRES_CODE_LANG_LUA,
    RRES_CODE_LANG_JS,
    RRES_CODE_LANG_PYTHON,
    RRES_CODE_LANG_RUST,
    RRES_CODE_LANG_ZIG,
    RRES_CODE_LANG_ODIN,
    RRES_CODE_LANG_JAI,
    RRES_CODE_LANG_GDSCRIPT,
    RRES_CODE_LANG_GLSL,
} rresCodeLang;

typedef enum rresPixelFormat {
    RRES_PIXELFORMAT_UNDEFINED = 0,
    RRES_PIXELFORMAT_UNCOMP_GRAYSCALE = 1,
    RRES_PIXELFORMAT_UNCOMP_GRAY_ALPHA,
    RRES_PIXELFORMAT_UNCOMP_R5G6B5,
    RRES_PIXELFORMAT_UNCOMP_R8G8B8,
    RRES_PIXELFORMAT_UNCOMP_R5G5B5A1,
    RRES_PIXELFORMAT_UNCOMP_R4G4B4A4,
    RRES_PIXELFORMAT_UNCOMP_R8G8B8A8,
    RRES_PIXELFORMAT_UNCOMP_R32,
    RRES_PIXELFORMAT_UNCOMP_R32G32B32,
    RRES_PIXELFORMAT_UNCOMP_R32G32B32A32,
    RRES_PIXELFORMAT_COMP_DXT1_RGB,
    RRES_PIXELFORMAT_COMP_DXT1_RGBA,
    RRES_PIXELFORMAT_COMP_DXT3_RGBA,
    RRES_PIXELFORMAT_COMP_DXT5_RGBA,
    RRES_PIXELFORMAT_COMP_ETC1_RGB,
    RRES_PIXELFORMAT_COMP_ETC2_RGB,
    RRES_PIXELFORMAT_COMP_ETC2_EAC_RGBA,
    RRES_PIXELFORMAT_COMP_PVRT_RGB,
    RRES_PIXELFORMAT_COMP_PVRT_RGBA,
    RRES_PIXELFORMAT_COMP_ASTC_4x4_RGBA,
    RRES_PIXELFORMAT_COMP_ASTC_8x8_RGBA
} rresPixelFormat;

typedef enum rresVertexAttribute {
    RRES_VERTEX_ATTRIBUTE_POSITION   = 0,
    RRES_VERTEX_ATTRIBUTE_TEXCOORD1  = 10,
    RRES_VERTEX_ATTRIBUTE_TEXCOORD2  = 11,
    RRES_VERTEX_ATTRIBUTE_TEXCOORD3  = 12,
    RRES_VERTEX_ATTRIBUTE_TEXCOORD4  = 13,
    RRES_VERTEX_ATTRIBUTE_NORMAL     = 20,
    RRES_VERTEX_ATTRIBUTE_TANGENT    = 30,
    RRES_VERTEX_ATTRIBUTE_COLOR      = 40,
    RRES_VERTEX_ATTRIBUTE_INDEX      = 100,
} rresVertexAttribute;

typedef enum rresVertexFormat {
    RRES_VERTEX_FORMAT_UBYTE = 0,
    RRES_VERTEX_FORMAT_BYTE,
    RRES_VERTEX_FORMAT_USHORT,
    RRES_VERTEX_FORMAT_SHORT,
    RRES_VERTEX_FORMAT_UINT,
    RRES_VERTEX_FORMAT_INT,
    RRES_VERTEX_FORMAT_HFLOAT,
    RRES_VERTEX_FORMAT_FLOAT,
} rresVertexFormat;

typedef enum rresFontStyle {
    RRES_FONT_STYLE_UNDEFINED = 0,
    RRES_FONT_STYLE_REGULAR,
    RRES_FONT_STYLE_BOLD,
    RRES_FONT_STYLE_ITALIC,
} rresFontStyle;

#ifdef __cplusplus
extern "C" {
#endif

RRESAPI rresResourceChunk rresLoadResourceChunk(const char *fileName, unsigned int rresId);
RRESAPI void rresUnloadResourceChunk(rresResourceChunk chunk);

RRESAPI rresResourceMulti rresLoadResourceMulti(const char *fileName, unsigned int rresId);
RRESAPI void rresUnloadResourceMulti(rresResourceMulti multi);

RRESAPI rresResourceChunkInfo rresLoadResourceChunkInfo(const char *fileName, unsigned int rresId);
RRESAPI rresResourceChunkInfo *rresLoadResourceChunkInfoAll(const char *fileName, unsigned int *chunkCount);

RRESAPI rresCentralDir rresLoadCentralDirectory(const char *fileName);
RRESAPI void rresUnloadCentralDirectory(rresCentralDir dir);

RRESAPI unsigned int rresGetDataType(const unsigned char *fourCC);
RRESAPI unsigned int rresGetResourceId(rresCentralDir dir, const char *fileName);
RRESAPI unsigned int rresComputeCRC32(const unsigned char *data, int len);

RRESAPI void rresSetCipherPassword(const char *pass);
RRESAPI const char *rresGetCipherPassword(void);

#ifdef __cplusplus
}
#endif

#endif // RRES_H

/***********************************************************************************
*   RRES IMPLEMENTATION
************************************************************************************/

#if defined(RRES_IMPLEMENTATION)

#if (defined(__STDC__) && __STDC_VERSION__ >= 199901L) || (defined(_MSC_VER) && _MSC_VER >= 1800)
    #include <stdbool.h>
#elif !defined(__cplusplus) && !defined(bool)
    typedef enum bool { false = 0, true = !false } bool;
    #define RL_BOOL_TYPE
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static const char *password = NULL;

static rresResourceChunkData rresLoadResourceChunkData(rresResourceChunkInfo info, void *packedData);

rresResourceChunk rresLoadResourceChunk(const char *fileName, unsigned int rresId)
{
    rresResourceChunk chunk = { 0 };
    FILE *rresFile = fopen(fileName, "rb");

    if (rresFile == NULL) RRES_LOG("RRES: WARNING: [%s] rres file could not be opened\n", fileName);
    else
    {
        rresFileHeader header = { 0 };
        fread(&header, sizeof(rresFileHeader), 1, rresFile);

        if (((header.id[0] == 'r') && (header.id[1] == 'r') && (header.id[2] == 'e') && (header.id[3] == 's')) && (header.version == 100))
        {
            bool found = false;
            for (int i = 0; i < header.chunkCount; i++)
            {
                rresResourceChunkInfo info = { 0 };
                fread(&info, sizeof(rresResourceChunkInfo), 1, rresFile);

                if (info.id == rresId)
                {
                    found = true;
                    if (info.nextOffset != 0) RRES_LOG("RRES: WARNING: Multiple linked resource chunks available for the provided id");

                    void *data = RRES_CALLOC(info.packedSize, 1);
                    fread(data, info.packedSize, 1, rresFile);

                    chunk.data = rresLoadResourceChunkData(info, data);
                    chunk.info = info;

                    RRES_FREE(data);
                    break;
                }
                else
                {
                    fseek(rresFile, info.packedSize, SEEK_CUR);
                }
            }
            if (!found) RRES_LOG("RRES: WARNING: Requested resource not found: 0x%08x\n", rresId);
        }
        else RRES_LOG("RRES: WARNING: The provided file is not a valid rres file\n");

        fclose(rresFile);
    }
    return chunk;
}

void rresUnloadResourceChunk(rresResourceChunk chunk)
{
    RRES_FREE(chunk.data.props);
    RRES_FREE(chunk.data.raw);
}

rresResourceMulti rresLoadResourceMulti(const char *fileName, unsigned int rresId)
{
    rresResourceMulti rres = { 0 };
    FILE *rresFile = fopen(fileName, "rb");

    if (rresFile == NULL) RRES_LOG("RRES: WARNING: [%s] rres file could not be opened\n", fileName);
    else
    {
        rresFileHeader header = { 0 };
        fread(&header, sizeof(rresFileHeader), 1, rresFile);

        if (((header.id[0] == 'r') && (header.id[1] == 'r') && (header.id[2] == 'e') && (header.id[3] == 's')) && (header.version == 100))
        {
            bool found = false;
            for (int i = 0; i < header.chunkCount; i++)
            {
                rresResourceChunkInfo info = { 0 };
                fread(&info, sizeof(rresResourceChunkInfo), 1, rresFile);

                if (info.id == rresId)
                {
                    found = true;
                    rres.count = 1;

                    long currentFileOffset = ftell(rresFile);
                    rresResourceChunkInfo temp = info;

                    while (temp.nextOffset != 0)
                    {
                        fseek(rresFile, temp.nextOffset, SEEK_SET);
                        fread(&temp, sizeof(rresResourceChunkInfo), 1, rresFile);
                        rres.count++;
                    }

                    rres.chunks = (rresResourceChunk *)RRES_CALLOC(rres.count, sizeof(rresResourceChunk));
                    fseek(rresFile, currentFileOffset, SEEK_SET);

                    void *data = RRES_CALLOC(info.packedSize, 1);
                    fread(data, info.packedSize, 1, rresFile);
                    rres.chunks[0].data = rresLoadResourceChunkData(info, data);
                    rres.chunks[0].info = info;
                    RRES_FREE(data);

                    int i2 = 1;
                    while (info.nextOffset != 0)
                    {
                        fseek(rresFile, info.nextOffset, SEEK_SET);
                        fread(&info, sizeof(rresResourceChunkInfo), 1, rresFile);

                        void *data2 = RRES_CALLOC(info.packedSize, 1);
                        fread(data2, info.packedSize, 1, rresFile);
                        rres.chunks[i2].data = rresLoadResourceChunkData(info, data2);
                        rres.chunks[i2].info = info;
                        RRES_FREE(data2);
                        i2++;
                    }

                    break;
                }
                else
                {
                    fseek(rresFile, info.packedSize, SEEK_CUR);
                }
            }
            if (!found) RRES_LOG("RRES: WARNING: Requested resource not found: 0x%08x\n", rresId);
        }
        else RRES_LOG("RRES: WARNING: The provided file is not a valid rres file\n");

        fclose(rresFile);
    }
    return rres;
}

void rresUnloadResourceMulti(rresResourceMulti multi)
{
    for (unsigned int i = 0; i < multi.count; i++) rresUnloadResourceChunk(multi.chunks[i]);
    RRES_FREE(multi.chunks);
}

RRESAPI rresResourceChunkInfo rresLoadResourceChunkInfo(const char *fileName, unsigned int rresId)
{
    rresResourceChunkInfo info = { 0 };
    FILE *rresFile = fopen(fileName, "rb");

    if (rresFile != NULL)
    {
        rresFileHeader header = { 0 };
        fread(&header, sizeof(rresFileHeader), 1, rresFile);

        if (((header.id[0] == 'r') && (header.id[1] == 'r') && (header.id[2] == 'e') && (header.id[3] == 's')) && (header.version == 100))
        {
            for (int i = 0; i < header.chunkCount; i++)
            {
                fread(&info, sizeof(rresResourceChunkInfo), 1, rresFile);
                if (info.id == rresId) break;
                else fseek(rresFile, info.packedSize, SEEK_CUR);
            }
        }
        fclose(rresFile);
    }
    return info;
}

RRESAPI rresResourceChunkInfo *rresLoadResourceChunkInfoAll(const char *fileName, unsigned int *chunkCount)
{
    rresResourceChunkInfo *infos = { 0 };
    unsigned int count = 0;
    FILE *rresFile = fopen(fileName, "rb");

    if (rresFile != NULL)
    {
        rresFileHeader header = { 0 };
        fread(&header, sizeof(rresFileHeader), 1, rresFile);

        if (((header.id[0] == 'r') && (header.id[1] == 'r') && (header.id[2] == 'e') && (header.id[3] == 's')) && (header.version == 100))
        {
            infos = (rresResourceChunkInfo *)RRES_CALLOC(header.chunkCount, sizeof(rresResourceChunkInfo));
            count = header.chunkCount;

            for (unsigned int i = 0; i < count; i++)
            {
                fread(&infos[i], sizeof(rresResourceChunkInfo), 1, rresFile);
                if (infos[i].nextOffset > 0) fseek(rresFile, infos[i].nextOffset, SEEK_SET);
                else fseek(rresFile, infos[i].packedSize, SEEK_CUR);
            }
        }
        fclose(rresFile);
    }
    *chunkCount = count;
    return infos;
}

rresCentralDir rresLoadCentralDirectory(const char *fileName)
{
    rresCentralDir dir = { 0 };
    FILE *rresFile = fopen(fileName, "rb");

    if (rresFile != NULL)
    {
        rresFileHeader header = { 0 };
        fread(&header, sizeof(rresFileHeader), 1, rresFile);

        if (((header.id[0] == 'r') && (header.id[1] == 'r') && (header.id[2] == 'e') && (header.id[3] == 's')) && (header.version == 100))
        {
            if (header.cdOffset == 0) RRES_LOG("RRES: WARNING: CDIR: No central directory found\n");
            else
            {
                rresResourceChunkInfo info = { 0 };
                fseek(rresFile, header.cdOffset, SEEK_CUR);
                fread(&info, sizeof(rresResourceChunkInfo), 1, rresFile);

                if ((info.type[0] == 'C') && (info.type[1] == 'D') && (info.type[2] == 'I') && (info.type[3] == 'R'))
                {
                    void *data = RRES_CALLOC(info.packedSize, 1);
                    fread(data, info.packedSize, 1, rresFile);

                    rresResourceChunkData chunkData = rresLoadResourceChunkData(info, data);
                    RRES_FREE(data);

                    dir.count = chunkData.props[0];

                    unsigned char *ptr = (unsigned char *)chunkData.raw;
                    dir.entries = (rresDirEntry *)RRES_CALLOC(dir.count, sizeof(rresDirEntry));

                    for (unsigned int i = 0; i < dir.count; i++)
                    {
                        dir.entries[i].id = ((int *)ptr)[0];
                        dir.entries[i].offset = ((int *)ptr)[1];
                        dir.entries[i].fileNameSize = ((int *)ptr)[3];

                        memcpy(dir.entries[i].fileName, ptr + 16, dir.entries[i].fileNameSize);
                        ptr += (16 + dir.entries[i].fileNameSize);
                    }

                    RRES_FREE(chunkData.props);
                    RRES_FREE(chunkData.raw);
                }
            }
        }
        fclose(rresFile);
    }
    return dir;
}

void rresUnloadCentralDirectory(rresCentralDir dir)
{
    RRES_FREE(dir.entries);
}

unsigned int rresGetDataType(const unsigned char *fourCC)
{
    unsigned int type = 0;
    if (fourCC != NULL)
    {
        if (memcmp(fourCC, "NULL", 4) == 0) type = RRES_DATA_NULL;
        else if (memcmp(fourCC, "RAWD", 4) == 0) type = RRES_DATA_RAW;
        else if (memcmp(fourCC, "TEXT", 4) == 0) type = RRES_DATA_TEXT;
        else if (memcmp(fourCC, "IMGE", 4) == 0) type = RRES_DATA_IMAGE;
        else if (memcmp(fourCC, "WAVE", 4) == 0) type = RRES_DATA_WAVE;
        else if (memcmp(fourCC, "VRTX", 4) == 0) type = RRES_DATA_VERTEX;
        else if (memcmp(fourCC, "FNTG", 4) == 0) type = RRES_DATA_FONT_GLYPHS;
        else if (memcmp(fourCC, "LINK", 4) == 0) type = RRES_DATA_LINK;
        else if (memcmp(fourCC, "CDIR", 4) == 0) type = RRES_DATA_DIRECTORY;
    }
    return type;
}

unsigned int rresGetResourceId(rresCentralDir dir, const char *fileName)
{
    unsigned int id = 0;
    for (unsigned int i = 0, len = 0; i < dir.count; i++)
    {
        len = (unsigned int)strlen(fileName);
        if (strncmp((const char *)dir.entries[i].fileName, fileName, len) == 0)
        {
            id = dir.entries[i].id;
            break;
        }
    }
    return id;
}

unsigned int rresComputeCRC32(const unsigned char *data, int len)
{
    static unsigned int crcTable[256] = {
        0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
        0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
        0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
        0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
        0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
        0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
        0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
        0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924, 0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
        0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
        0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
        0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
        0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
        0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
        0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
        0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
        0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
        0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
        0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
        0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
        0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
        0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
        0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
        0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236, 0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
        0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
        0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
        0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
        0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
        0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
        0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
        0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
        0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
        0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
    };

    unsigned int crc = ~0u;
    for (int i = 0; i < len; i++) crc = (crc >> 8)^crcTable[data[i]^(crc&0xff)];
    return ~crc;
}

void rresSetCipherPassword(const char *pass) { password = pass; }

const char *rresGetCipherPassword(void)
{
    if (password == NULL) password = "password12345";
    return password;
}

static rresResourceChunkData rresLoadResourceChunkData(rresResourceChunkInfo info, void *data)
{
    rresResourceChunkData chunkData = { 0 };
    unsigned int crc32 = rresComputeCRC32((const unsigned char *)data, info.packedSize);

    if ((rresGetDataType(info.type) != RRES_DATA_NULL) && (crc32 == info.crc32))
    {
        if ((info.compType == RRES_COMP_NONE) && (info.cipherType == RRES_CIPHER_NONE))
        {
            chunkData.propCount = ((unsigned int *)data)[0];

            if (chunkData.propCount > 0)
            {
                chunkData.props = (unsigned int *)RRES_CALLOC(chunkData.propCount, sizeof(unsigned int));
                for (unsigned int i = 0; i < chunkData.propCount; i++) chunkData.props[i] = ((unsigned int *)data)[i + 1];
            }

            int rawSize = info.baseSize - sizeof(int) - (chunkData.propCount*sizeof(int));
            chunkData.raw = RRES_CALLOC(rawSize, 1);
            if (chunkData.raw != NULL) memcpy(chunkData.raw, ((unsigned char *)data) + sizeof(int) + (chunkData.propCount*sizeof(int)), rawSize);
        }
        else
        {
            chunkData.raw = RRES_CALLOC(info.packedSize, 1);
            if (chunkData.raw != NULL) memcpy(chunkData.raw, (unsigned char *)data, info.packedSize);
        }
    }

    if (crc32 != info.crc32) RRES_LOG("RRES: WARNING: [ID %i] CRC32 does not match, data can be corrupted\n", info.id);

    return chunkData;
}

#endif // RRES_IMPLEMENTATION
