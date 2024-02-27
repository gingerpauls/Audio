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
    //RIFF
    char RIFFID[CHUNK_ID_SIZE];
    unsigned int RIFFSize;
    char RIFFFormType[CHUNK_ID_SIZE];
    //fmt 
    char fmtID[CHUNK_ID_SIZE];
    unsigned int fmtSize;
    unsigned short AudioFormat;
    unsigned short NumChannels;
    unsigned int SampleRate;
    unsigned int ByteRate;
    unsigned short BlockAlign;
    unsigned short BitsPerSample;
    //LIST
    char listID[CHUNK_ID_SIZE];
    unsigned int listSize;
    char listType[CHUNK_ID_SIZE];
    char *listString;
    unsigned int numInfo;
    //info
    //char infoID[CHUNK_ID_SIZE];
    //unsigned int infoSize;
    //char *infoString;
    //junk
    char junkID[CHUNK_ID_SIZE];
    unsigned int junkSize;
    char *junkString;
    //data
    char dataID[CHUNK_ID_SIZE];
    unsigned int dataSize;
    short *Data; // TODO change to float 
    //total file size in byte
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
    //float *dataBuffer = (float *) Buffer;
    //wave->data = (float *) malloc(wave->file_size);
    //wave->file_size = wave->file_size;

    //for(size_t i = 0; i < BufferLength * ChannelCount; i += ChannelCount) {
    //    for(size_t j = 0; j < ChannelCount; j++) {
    //        dataBuffer[i + j] = wave->Data[i];
    //    }
    //}
}

// if able to write at least one frame, but runs out of data, then write silence to remaining frames
// if not able to write at least one frame, write nothing to buffer (not even silence), then write AUDCLNT_BUFFERFLAGS_SILENT to flags
class  MyAudioSource {
    public:
    HRESULT LoadData(UINT32 bufferFrameCount, BYTE *pData, DWORD *flags, WAVE *rawfile) {
        HRESULT hr = NULL;
        GenerateSineSamples(pData, bufferFrameCount, 440, 2, 192000, 1, 0);
        //LoadRAW(pData, bufferFrameCount, 2, 192000, rawfile);
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
    //err = fopen_s(&wav, "sounds/sine_44k_16b_1ch.wav", "rb");
    err = fopen_s(&wav, "sounds/ambient-swoosh.wav", "rb");
    //err = fopen_s(&wav, "sounds/ambient-drop.wav", "rb");
    //err = fopen_s(&wav, "sounds/sine_192k_32b_2ch.wav", "rb");
    //err = fopen_s(&wav, "sounds/message.wav", "rb");
    //err = fopen_s(&wav, "sounds/Bombtrack.wav", "rb");
    if(err != 0) {
        perror("fopen\n");
    }

    WAVE wavefile1 = {0};
    wavefile1.Data = NULL;
    char chunk_id[CHUNK_ID_SIZE];
    int count = 0;
    info info;

    fseek(wav, 0, SEEK_END);
    wavefile1.file_size = ftell(wav);
    rewind(wav);

    count = fread_s(&chunk_id, CHUNK_ID_SIZE, 1, CHUNK_ID_SIZE, wav);
    if(ferror(wav)) {
        perror("read error\n");
    }
    fseek(wav, -CHUNK_ID_SIZE, SEEK_CUR);
    if(strncmp(chunk_id, "RIFF", CHUNK_ID_SIZE) == 0) {
        count = fread_s(&wavefile1.RIFFID, sizeof(wavefile1.RIFFID), 1, sizeof(wavefile1.RIFFID), wav);
        count = fread_s(&wavefile1.RIFFSize, sizeof(wavefile1.RIFFSize), 1, sizeof(wavefile1.RIFFSize), wav);
        count = fread_s(&wavefile1.RIFFFormType, sizeof(wavefile1.RIFFFormType), 1, sizeof(wavefile1.RIFFFormType), wav);
    }
    printf("RIFFID: \t%.4s\n", &wavefile1.RIFFID);
    printf("RIFFSize: \t%u\n", wavefile1.RIFFSize);
    printf("RIFFFormType: \t%.4s\n", &wavefile1.RIFFFormType);

    count = fread_s(&chunk_id, CHUNK_ID_SIZE, 1, CHUNK_ID_SIZE, wav);
    fseek(wav, -CHUNK_ID_SIZE, SEEK_CUR);
    if(strncmp(chunk_id, "fmt ", CHUNK_ID_SIZE) == 0) {
        count = fread_s(&wavefile1.fmtID, sizeof(wavefile1.fmtID), 1, sizeof(wavefile1.fmtID), wav);
        count = fread_s(&wavefile1.fmtSize, sizeof(wavefile1.fmtSize), 1, sizeof(wavefile1.fmtSize), wav);
        count = fread_s(&wavefile1.AudioFormat, sizeof(wavefile1.AudioFormat), 1, sizeof(wavefile1.AudioFormat), wav);
        count = fread_s(&wavefile1.NumChannels, sizeof(wavefile1.NumChannels), 1, sizeof(wavefile1.NumChannels), wav);
        count = fread_s(&wavefile1.SampleRate, sizeof(wavefile1.SampleRate), 1, sizeof(wavefile1.SampleRate), wav);
        count = fread_s(&wavefile1.ByteRate, sizeof(wavefile1.ByteRate), 1, sizeof(wavefile1.ByteRate), wav);
        count = fread_s(&wavefile1.BlockAlign, sizeof(wavefile1.BlockAlign), 1, sizeof(wavefile1.BlockAlign), wav);
        count = fread_s(&wavefile1.BitsPerSample, sizeof(wavefile1.BitsPerSample), 1, sizeof(wavefile1.BitsPerSample), wav);
    }
    printf("fmtID: \t\t%.4s\n", &wavefile1.fmtID);
    printf("fmtSize: \t%u\n", wavefile1.fmtSize);
    printf("AudioFormat: \t%u\n", wavefile1.AudioFormat);
    printf("NumChannels: \t%u\n", wavefile1.NumChannels);
    printf("SampleRate: \t%u\n", wavefile1.SampleRate);
    printf("ByteRate: \t%u\n", wavefile1.ByteRate);
    printf("BlockAlign: \t%u\n", wavefile1.BlockAlign);
    printf("BitsPerSample: \t%u\n", wavefile1.BitsPerSample);

    // TODO turn all of this into a loop
    count = fread_s(&chunk_id, CHUNK_ID_SIZE, 1, CHUNK_ID_SIZE, wav);
    fseek(wav, -CHUNK_ID_SIZE, SEEK_CUR);
    if(strncmp(chunk_id, "LIST", CHUNK_ID_SIZE) == 0) {
        count = fread_s(&wavefile1.listID, sizeof(wavefile1.listID), 1, sizeof(wavefile1.listID), wav);
        count = fread_s(&wavefile1.listSize, sizeof(wavefile1.listSize), 1, sizeof(wavefile1.listSize), wav);
        count = fread_s(&wavefile1.listType, sizeof(wavefile1.listType), 1, sizeof(wavefile1.listType), wav);
        printf("listID: \t%.4s\n", &wavefile1.listID);
        printf("listSize: \t%u\n", wavefile1.listSize);
        printf("listType: \t%.4s\n", &wavefile1.listType);

        if(strncmp(wavefile1.listType, "INFO", CHUNK_ID_SIZE) == 0) {
            do {
                count = fread_s(&chunk_id, CHUNK_ID_SIZE, 1, CHUNK_ID_SIZE, wav);
                fseek(wav, -CHUNK_ID_SIZE, SEEK_CUR);
                if(strncmp(chunk_id, "data", 4) != 0) {
                    count = fread_s(&info.infoID, sizeof(info.infoID), 1, sizeof(info.infoID), wav);
                    count = fread_s(&info.infoSize, sizeof(info.infoSize), 1, sizeof(info.infoSize), wav);
                    info.infoString = (char*)malloc(info.infoSize);
                    count = fread_s(info.infoString, info.infoSize, 1, info.infoSize, wav);
                    printf("%.4s \t\t%s\n", &info.infoID, info.infoString);
                    free(info.infoString);
                    fseek(wav, 1, SEEK_CUR); // TODO why is this needed?
                }
                count = fread_s(&chunk_id, CHUNK_ID_SIZE, 1, CHUNK_ID_SIZE, wav);
                fseek(wav, -CHUNK_ID_SIZE, SEEK_CUR);
                if(strncmp(chunk_id, "data", 4) != 0) {
                    count = fread_s(&info.infoID, sizeof(info.infoID), 1, sizeof(info.infoID), wav);
                    count = fread_s(&info.infoSize, sizeof(info.infoSize), 1, sizeof(info.infoSize), wav);
                    info.infoString = (char*)malloc(info.infoSize);
                    count = fread_s(info.infoString, info.infoSize, 1, info.infoSize, wav);
                    printf("%.4s \t\t%s\n", &info.infoID, info.infoString);
                    free(info.infoString);
                    //fseek(wav, 0, SEEK_CUR); // TODO and this is not?
                }
            }
            while(strncmp(chunk_id, "data", 4) != 0);
        }
    }
    // TODO finish junk chunk
    if(strncmp(chunk_id, "junk", CHUNK_ID_SIZE) == 0) {
        count = fread_s(&wavefile1.junkID, sizeof(wavefile1.junkID), 1, sizeof(wavefile1.junkID), wav);
        count = fread_s(&wavefile1.junkSize, sizeof(wavefile1.junkSize), 1, sizeof(wavefile1.junkSize), wav);
        //wavefile1.junkString = malloc(wavefile1.junkSize);
        //count = fread_s(&wavefile1.junkString, wavefile1.junkString, 1, wavefile1.junkString, wav);
        printf("junkID: \t%.4s\n", &wavefile1.junkID);
        printf("junkSize: \t%u\n", wavefile1.junkSize);
        fseek(wav, wavefile1.junkSize, SEEK_CUR);
        //printf("junkString: \t%s\n", &wavefile1.junkString);
        //free(wavefile1.junkString);
    }

    count = fread_s(&chunk_id, CHUNK_ID_SIZE, 1, CHUNK_ID_SIZE, wav);
    fseek(wav, -CHUNK_ID_SIZE, SEEK_CUR);
    if(strncmp(chunk_id, "data", 4) == 0) {
        count = fread_s(&wavefile1.dataID, sizeof(wavefile1.dataID), 1, sizeof(wavefile1.dataID), wav);
        count = fread_s(&wavefile1.dataSize, sizeof(wavefile1.dataSize), 1, sizeof(wavefile1.dataSize), wav);
        printf("dataID: \t%.4s\n", &wavefile1.dataID);
        printf("dataSize: \t%u\n", wavefile1.dataSize);
        wavefile1.Data = (short*)malloc(wavefile1.dataSize);
        //TODO why does this cause printf to cause exception
        //count = fread_s(&wavefile1.Data, wavefile1.dataSize, 1, wavefile1.dataSize / sizeof(*wavefile1.Data), wav);
        int total_count = 0;
        for(size_t i = 0; i < wavefile1.dataSize / sizeof(*wavefile1.Data); i++) {
            count = fread_s(&wavefile1.Data[i], wavefile1.dataSize, sizeof(*wavefile1.Data), 1, wav);
            printf("Data[%i]: \t%hi\n", i, wavefile1.Data[i]);
            total_count += count;
        }
        free(wavefile1.Data);
        printf("total_count: %i\n", total_count);
    }
    fclose(wav);

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
        hr = pMySource->LoadData(bufferFrameCount, pData, &flags, &wavefile1);
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