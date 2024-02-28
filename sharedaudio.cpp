#define _CRT_SECURE_NO_WARNINGS
#include "AudioClient.h"
#include "mmdeviceapi.h"
#include "time.h"
#include "math.h"
#include "stdio.h"
#include "stdlib.h"

#define M_PI 3.14159265358979323846
#define CHUNK_ID_SIZE 4
#define REFTIMES_PER_SEC  10000000 // I believe this is because clock are 100ns?
#define REFTIMES_PER_MILLISEC  10000
#define EXIT_ON_ERROR(hres)  \
              if (FAILED(hres)) { goto Exit; }
#define SAFE_RELEASE(punk)  \
              if ((punk) != NULL)  \
                { (punk)->Release(); (punk) = NULL; }

typedef struct {
    char ID[CHUNK_ID_SIZE];
    unsigned int Size;
    char FormType[CHUNK_ID_SIZE];
} RIFF;

typedef struct {
    char ID[CHUNK_ID_SIZE];
    unsigned int Size;
    unsigned short AudioFormat;
    unsigned short NumChannels;
    unsigned int SampleRate;
    unsigned int ByteRate;
    unsigned short BlockAlign;
    unsigned short BitsPerSample;
} FMT;

typedef struct {
    char ID[CHUNK_ID_SIZE];
    unsigned int Size;
    char Type[CHUNK_ID_SIZE];
    char *String;
} LIST;

typedef struct {
    char ID[CHUNK_ID_SIZE];
    unsigned int Size;
    char *String;
} INFO;

typedef struct {
    char ID[CHUNK_ID_SIZE];
    unsigned int Size;
    char *String;
} JUNK;

typedef struct {
    char ID[CHUNK_ID_SIZE];
    unsigned int Size;
    int *Data;
} DATA;

typedef struct {
    RIFF RIFF;
    FMT FMT;
    LIST LIST;
    INFO INFO;
    JUNK JUNK;
    DATA DATA;
    unsigned long file_size;
} WAVE;

typedef struct {
    char infoID[CHUNK_ID_SIZE];
    unsigned int infoSize;
    char *infoString;
} info;

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient = __uuidof(IAudioClient);
const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);

void PrintFilePos(FILE *wav) {
    int file_pos = ftell(wav);
    printf("file_pos: \t%i\n", file_pos);
}

void GenerateSineSamples(BYTE *Buffer, size_t BufferLength, DWORD Frequency, WORD ChannelCount, DWORD SamplesPerSecond, double Amplitude, double *InitialTheta) {

    double sampleIncrement = (Frequency * (M_PI * 2)) / (double) SamplesPerSecond;
    double theta = (InitialTheta != NULL ? *InitialTheta : 0);
    float *dataBuffer = (float *) Buffer;

    for(size_t i = 0; i < BufferLength * ChannelCount; i += ChannelCount) {
        double sinValue = Amplitude * sin(theta);
        for(size_t j = 0; j < ChannelCount; j++) {
            dataBuffer[i + j] = (float) (sinValue);
        }
        theta += sampleIncrement;
    }

    if(InitialTheta != NULL) {
        *InitialTheta = theta;
    }
}

void LoadWAVE(BYTE *Buffer, size_t BufferLength, WORD ChannelCount, DWORD SamplesPerSecond, WAVE *wave) {
    float *dataBuffer = (float *) Buffer;
    for(size_t i = 0; i < BufferLength * ChannelCount; i += ChannelCount) {
        for(size_t j = 0; j < ChannelCount; j++) {
            dataBuffer[i + j] = (float)(wave->DATA.Data[i]) / LONG_MAX;
        }
    }
}

// if able to write at least one frame, but runs out of data, then write silence to remaining frames
// if not able to write at least one frame, write nothing to buffer (not even silence), then write AUDCLNT_BUFFERFLAGS_SILENT to flags
class  MyAudioSource {
    public:
    HRESULT LoadData(UINT32 bufferFrameCount, BYTE *pData, DWORD *flags, WAVE *wave) {
        HRESULT hr = NULL;
        //GenerateSineSamples(pData, bufferFrameCount, 440, wave->FMT.NumChannels, wave->FMT.SampleRate, 1, 0);
        LoadWAVE(pData, bufferFrameCount, 2, wave->FMT.SampleRate, wave);
        return hr;
    }
    HRESULT SetFormat(WAVEFORMATEX *pwfx) {
        HRESULT hr = NULL;
        pwfx->wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        return hr;
    }
};

HRESULT PlayAudioStream(MyAudioSource *pMySource) {
    HRESULT hr;
    REFERENCE_TIME hnsRequestedDuration = REFTIMES_PER_SEC;
    REFERENCE_TIME hnsActualDuration;
    IMMDeviceEnumerator *pEnumerator = NULL;
    IMMDevice *pDevice = NULL;
    IAudioClient *pAudioClient = NULL;
    IAudioRenderClient *pRenderClient = NULL;
    WAVEFORMATEX *pwfx = NULL;
    UINT32 bufferFrameCount;
    UINT32 numFramesAvailable;
    UINT32 numFramesPadding;
    BYTE *pData;
    DWORD flags = 0;

    FILE *wav = NULL;
    errno_t err;
    err = fopen_s(&wav, "sounds/sine_44k_16b_1ch.wav", "rb");
    //err = fopen_s(&wav, "sounds/ambient-swoosh.wav", "rb");
    //err = fopen_s(&wav, "sounds/resonant-twang.wav", "rb");
    //err = fopen_s(&wav, "sounds/sine_192k_32b_2ch.wav", "rb");
    //err = fopen_s(&wav, "sounds/vocal-hah.wav", "rb");
    //err = fopen_s(&wav, "sounds/synthetic-gib.wav", "rb");

    if(err != 0) {
        perror("fopen\n");
    }

    WAVE wav1 = {0};
    wav1.DATA.Data = NULL;
    char chunk_id[CHUNK_ID_SIZE];
    int count = 0;
    INFO INFO;

    fseek(wav, 0, SEEK_END);
    wav1.file_size = ftell(wav);
    rewind(wav);
    do {
        count = fread_s(&chunk_id, CHUNK_ID_SIZE, 1, CHUNK_ID_SIZE, wav);
        if(ferror(wav)) {
            perror("read error\n");
        }
        fseek(wav, -CHUNK_ID_SIZE, SEEK_CUR);

        if(strncmp(chunk_id, "RIFF", CHUNK_ID_SIZE) == 0) {
            count = fread_s(&wav1.RIFF.ID, sizeof(wav1.RIFF.ID), 1, sizeof(wav1.RIFF.ID), wav);
            count = fread_s(&wav1.RIFF.Size, sizeof(wav1.RIFF.Size), 1, sizeof(wav1.RIFF.Size), wav);
            count = fread_s(&wav1.RIFF.FormType, sizeof(wav1.RIFF.FormType), 1, sizeof(wav1.RIFF.FormType), wav);
            printf("ID: \t%.4s\n", &wav1.RIFF.ID);
            printf("Size: \t%u\n", wav1.RIFF.Size);
            printf("FormType: \t%.4s\n", &wav1.RIFF.FormType);
            continue;
        }

        if(strncmp(chunk_id, "fmt ", CHUNK_ID_SIZE) == 0) {
            count = fread_s(&wav1.FMT.ID, sizeof(wav1.FMT.ID), 1, sizeof(wav1.FMT.ID), wav);
            count = fread_s(&wav1.FMT.Size, sizeof(wav1.FMT.Size), 1, sizeof(wav1.FMT.Size), wav);
            count = fread_s(&wav1.FMT.AudioFormat, sizeof(wav1.FMT.AudioFormat), 1, sizeof(wav1.FMT.AudioFormat), wav);
            count = fread_s(&wav1.FMT.NumChannels, sizeof(wav1.FMT.NumChannels), 1, sizeof(wav1.FMT.NumChannels), wav);
            count = fread_s(&wav1.FMT.SampleRate, sizeof(wav1.FMT.SampleRate), 1, sizeof(wav1.FMT.SampleRate), wav);
            count = fread_s(&wav1.FMT.ByteRate, sizeof(wav1.FMT.ByteRate), 1, sizeof(wav1.FMT.ByteRate), wav);
            count = fread_s(&wav1.FMT.BlockAlign, sizeof(wav1.FMT.BlockAlign), 1, sizeof(wav1.FMT.BlockAlign), wav);
            count = fread_s(&wav1.FMT.BitsPerSample, sizeof(wav1.FMT.BitsPerSample), 1, sizeof(wav1.FMT.BitsPerSample), wav);
            printf("ID: \t\t%.4s\n", &wav1.FMT.ID);
            printf("Size: \t%u\n", wav1.FMT.Size);
            printf("AudioFormat: \t%u\n", wav1.FMT.AudioFormat);
            printf("NumChannels: \t%u\n", wav1.FMT.NumChannels);
            printf("SampleRate: \t%u\n", wav1.FMT.SampleRate);
            printf("ByteRate: \t%u\n", wav1.FMT.ByteRate);
            printf("BlockAlign: \t%u\n", wav1.FMT.BlockAlign);
            printf("BitsPerSample: \t%u\n", wav1.FMT.BitsPerSample);
            continue;
        }

        if(strncmp(chunk_id, "LIST", CHUNK_ID_SIZE) == 0) {
            count = fread_s(&wav1.LIST.ID, sizeof(wav1.LIST.ID), 1, sizeof(wav1.LIST.ID), wav);
            count = fread_s(&wav1.LIST.Size, sizeof(wav1.LIST.Size), 1, sizeof(wav1.LIST.Size), wav);
            count = fread_s(&wav1.LIST.Type, sizeof(wav1.LIST.Type), 1, sizeof(wav1.LIST.Type), wav);
            if(strncmp(wav1.LIST.Type, "INFO", CHUNK_ID_SIZE) == 0) {
                while(1) {
                    count = fread_s(&INFO.ID, sizeof(INFO.ID), 1, sizeof(INFO.ID), wav);
                    count = fread_s(&INFO.Size, sizeof(INFO.Size), 1, sizeof(INFO.Size), wav);
                    INFO.String = (char*)malloc(INFO.Size);
                    count = fread_s(INFO.String, INFO.Size, 1, INFO.Size, wav);
                    printf("%.4s \t\t%s\n", &INFO.ID, INFO.String);
                    free(INFO.String);
                    if(INFO.Size % 2) {
                        fseek(wav, 1, SEEK_CUR);
                    }
                    count = fread_s(&chunk_id, CHUNK_ID_SIZE, 1, CHUNK_ID_SIZE, wav);
                    fseek(wav, -CHUNK_ID_SIZE, SEEK_CUR);
                    if(strncmp(chunk_id, "data", 4) == 0) {
                        break;
                    }
                }
            }
            else {
                printf("unknown list type\n");
            }
            printf("ID: \t%.4s\n", &wav1.LIST.ID);
            printf("Size: \t%u\n", wav1.LIST.Size);
            printf("Type: \t%.4s\n", &wav1.LIST.Type);
            continue;
        }

        // TODO finish junk chunk - sometimes has data in junk??
        if((strncmp(chunk_id, "JUNK", CHUNK_ID_SIZE) == 0) || (strncmp(chunk_id, "junk", CHUNK_ID_SIZE) == 0)) {
            count = fread_s(&wav1.JUNK.ID, sizeof(wav1.JUNK.ID), 1, sizeof(wav1.JUNK.ID), wav);
            count = fread_s(&wav1.JUNK.Size, sizeof(wav1.JUNK.Size), 1, sizeof(wav1.JUNK.Size), wav);
            fseek(wav, wav1.JUNK.Size, SEEK_CUR);
            printf("ID: \t%.4s\n", &wav1.JUNK.ID);
            printf("Size: \t%u\n", wav1.JUNK.Size);
            continue;
        }

        if(strncmp(chunk_id, "data", 4) == 0) {
            count = fread_s(&wav1.DATA.ID, sizeof(wav1.DATA.ID), 1, sizeof(wav1.DATA.ID), wav);
            count = fread_s(&wav1.DATA.Size, sizeof(wav1.DATA.Size), 1, sizeof(wav1.DATA.Size), wav);
            printf("ID: \t%.4s\n", &wav1.DATA.ID);
            printf("Size: \t%u\n", wav1.DATA.Size);
            wav1.DATA.Data = (int*)malloc(wav1.DATA.Size);
            int total_count = 0;
            for(size_t i = 0; i < wav1.DATA.Size / sizeof(*wav1.DATA.Data); i++) {
                count = fread_s(&wav1.DATA.Data[i], wav1.DATA.Size, sizeof(*wav1.DATA.Data), 1, wav);
                //printf("Data[%i]: \t%i\n", i, wav1.DATA.Data[i]);
                total_count += count;
            }
            //free(wav1.DATA.Data);
            printf("total_count: %i\n", total_count);
            break;
        }
    }
    while(1);

    //fclose(wav);

    printf("program success\n");




    pData = NULL;

    hr = CoInitialize(nullptr);

    hr = CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, IID_IMMDeviceEnumerator, (void **) &pEnumerator);
    EXIT_ON_ERROR(hr);

    hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    EXIT_ON_ERROR(hr);

    hr = pDevice->Activate(IID_IAudioClient, CLSCTX_ALL, NULL, (void **) &pAudioClient);
    EXIT_ON_ERROR(hr);

    hr = pAudioClient->GetMixFormat(&pwfx);
    EXIT_ON_ERROR(hr);

    hr = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, hnsRequestedDuration, 0, pwfx, NULL);
    EXIT_ON_ERROR(hr);

    // Tell the audio source which format to use.
    hr = pMySource->SetFormat(pwfx);
    EXIT_ON_ERROR(hr);

    // Get the actual size of the allocated buffer.
    hr = pAudioClient->GetBufferSize(&bufferFrameCount);
    EXIT_ON_ERROR(hr);

    hr = pAudioClient->GetService(IID_IAudioRenderClient, (void **) &pRenderClient);
    EXIT_ON_ERROR(hr);
    while(1) {
        // retrieves the next packet so that the client can fill it with rendering data
        // Grab the entire buffer for the initial fill operation.
        hr = pRenderClient->GetBuffer(bufferFrameCount, &pData);
        EXIT_ON_ERROR(hr);

        // Load the initial data into the shared buffer.
        hr = pMySource->LoadData(bufferFrameCount, pData, &flags, &wav1);
        EXIT_ON_ERROR(hr);

        hr = pRenderClient->ReleaseBuffer(bufferFrameCount, flags);
        EXIT_ON_ERROR(hr);

        // Calculate the actual duration of the allocated buffer.
        hnsActualDuration = (double) REFTIMES_PER_SEC * bufferFrameCount / pwfx->nSamplesPerSec;

        printf("audio start\n");
        hr = pAudioClient->Start();  // Start playing.
        EXIT_ON_ERROR(hr);

        // Each loop fills about half of the shared buffer.
        //while(flags != AUDCLNT_BUFFERFLAGS_SILENT) {
        //    printf("loop start\n");
        //    // Sleep for half the buffer duration.
        //    Sleep((DWORD) (hnsActualDuration / REFTIMES_PER_MILLISEC / 1));
        //    // See how much buffer space is available.
        //    hr = pAudioClient->GetCurrentPadding(&numFramesPadding);
        //    EXIT_ON_ERROR(hr);
        //    numFramesAvailable = bufferFrameCount - numFramesPadding;a
        //    // Grab all the available space in the shared buffer.
        //    hr = pRenderClient->GetBuffer(numFramesAvailable, &pData);
        //    EXIT_ON_ERROR(hr);
        //    // Get next 1/2-second of data from the audio source.
        //    hr = pMySource->LoadData(numFramesAvailable, pData, &flags);
        //    EXIT_ON_ERROR(hr);
        //    hr = pRenderClient->ReleaseBuffer(numFramesAvailable, flags);
        //    EXIT_ON_ERROR(hr);
        //    printf("loop end\n");
        //}

        // Wait for last data in buffer to play before stopping.
        Sleep((DWORD) (hnsActualDuration / REFTIMES_PER_MILLISEC / 1));

        hr = pAudioClient->Stop();  // Stop playing.
        EXIT_ON_ERROR(hr);
    }

Exit:
    CoTaskMemFree(pwfx);
    SAFE_RELEASE(pEnumerator);
    SAFE_RELEASE(pDevice);
    SAFE_RELEASE(pAudioClient);
    SAFE_RELEASE(pRenderClient);
    //SAFE_RELEASE(pData);
    //free(pData); // delete?
    //delete pData;

    return hr;
}

int main(void) {
    MyAudioSource pMySource;
    PlayAudioStream(&pMySource);
    return 0;
}