#define _CRT_SECURE_NO_WARNINGS
#include "AudioClient.h"
#include "mmdeviceapi.h"
#include "time.h"
#include "math.h"
#include "stdio.h"
#include "stdlib.h"

#define M_PI 3.14159265358979323846

// REFERENCE_TIME time units per second and per millisecond
#define REFTIMES_PER_SEC  10000000
#define REFTIMES_PER_MILLISEC  10000

#define EXIT_ON_ERROR(hres)  \
              if (FAILED(hres)) { goto Exit; }


#define SAFE_RELEASE(punk)  \
              if ((punk) != NULL)  \
                { (punk)->Release(); (punk) = NULL; }

typedef struct {
    float *Data;
    unsigned long file_size;
    int num_elements;
} RAW;

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient = __uuidof(IAudioClient);
const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);

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

void LoadRAW(BYTE *Buffer, size_t BufferLength, WORD ChannelCount, DWORD SamplesPerSecond, RAW *rawfile) {
    float *dataBuffer = (float *) Buffer;
    RAW rawlarge = {0};
    rawlarge.Data = (float *) malloc(rawfile->file_size);
    rawlarge.file_size = rawfile->file_size;

    //for(size_t i = 0; i < BufferLength * ChannelCount; i += ChannelCount) {
    //    for(size_t j = 0; j < ChannelCount; j++) {
    //        dataBuffer[i + j] = rawfile->Data[i];
    //    }
    //}
}

// if able to write at least one frame, but runs out of data, then write silence to remaining frames
// if not able to write at least one frame, write nothing to buffer (not even silence), then write AUDCLNT_BUFFERFLAGS_SILENT to flags
class  MyAudioSource {
    public:
    HRESULT LoadData(UINT32 bufferFrameCount, BYTE *pData, DWORD *flags, RAW *rawfile) {
        HRESULT hr = NULL;
        //GenerateSineSamples(pData, bufferFrameCount, 440, 2, 192000, 1, 0);
        LoadRAW(pData, bufferFrameCount, 2, 192000, rawfile);
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
    RAW wavfile1 = {0};
    errno_t err;
    int int_error;

    wav = fopen("sine.wav", "r+");
    fseek(wav, 0, SEEK_END);
    wavfile1.file_size = ftell(wav);
    rewind(wav);

    wavfile1.Data = (float*)malloc(wavfile1.file_size);
    if(wavfile1.Data == NULL) {
        printf("wave1.data malloc failed\n");
    }

    int count = 0;
    count = fread_s(wavfile1.Data, wavfile1.file_size, sizeof(*wavfile1.Data), (wavfile1.file_size / sizeof(*wavfile1.Data)), wav);
    if(ferror(wav)) {
        perror("Read error");
    }
    //for(size_t i = 0; i < rawfile1.file_size / sizeof(*rawfile1.Data); i++) {
    //    printf("Data %f\n", rawfile1.Data[i]);
    //}
    wavfile1.num_elements = count;


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
        hr = pMySource->LoadData(bufferFrameCount, pData, &flags, &wavfile1);
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