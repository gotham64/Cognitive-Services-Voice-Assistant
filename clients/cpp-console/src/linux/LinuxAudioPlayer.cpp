// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <cstring>
#include <chrono>
#include <condition_variable>
#include <string>
#include <thread>
#include <mutex>
#include <alsa/asoundlib.h>
#include "LinuxAudioPlayer.h"

using namespace AudioPlayer;

LinuxAudioPlayer::LinuxAudioPlayer(){
    
    Open();
    m_playerThread = std::thread(&LinuxAudioPlayer::PlayerThreadMain, this);
    
}

LinuxAudioPlayer::~LinuxAudioPlayer(){
    Close();
    m_canceled = true;
    m_threadMutex.unlock();
    m_conditionVariable.notify_one();
    m_playerThread.join();
}

int LinuxAudioPlayer::Open(){
    int rc;
    rc = Open("default", AudioPlayerFormat::Mono16khz16bit);
    return rc;
}

int LinuxAudioPlayer::SetVolume(unsigned int percent){
    //stubbed out
    return 0;
}

int LinuxAudioPlayer::Open(const std::string& device, AudioPlayerFormat format){
    //PCM variables
    int rc;
    int err;
    unsigned int val;
    int dir;

    m_device = device;

    //begin PCM setup
    
    /* Open PCM device for playback. */
    if ((err = snd_pcm_open(&m_playback_handle, device.c_str(), SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
    fprintf(stderr, "cannot open output audio device %s: %s\n", device.c_str(), snd_strerror(err));
    exit(1);
    }
    
    /* Allocate a hardware parameters object. */
    snd_pcm_hw_params_alloca(&m_params);

    /* Fill it in with default values. */
    snd_pcm_hw_params_any(m_playback_handle, m_params);

    /* Set the desired hardware parameters. */

    /* Interleaved mode */
    snd_pcm_hw_params_set_access(m_playback_handle, m_params,
                        SND_PCM_ACCESS_RW_INTERLEAVED);

    switch(format){
        case AudioPlayerFormat::Mono16khz16bit:
        default:
            /* Signed 16-bit little-endian format */
            fprintf(stdout, "Format = Mono16khz16bit\n");
            m_numChannels = 1;
            m_bytesPerSample = 2;
            m_bitsPerSecond = 16000;
            snd_pcm_hw_params_set_format(m_playback_handle, m_params,
                                SND_PCM_FORMAT_S16_LE);
    }

    /* set number of Channels */
    snd_pcm_hw_params_set_channels(m_playback_handle, m_params, m_numChannels);

    /* set bits/second sampling rate */
    snd_pcm_hw_params_set_rate_near(m_playback_handle, m_params,
                                    &m_bitsPerSecond, &dir);
                                    
    /* Set buffer size */
    unsigned int bufferSize = 1024;
    snd_pcm_hw_params_set_buffer_size_near(m_playback_handle, m_params, (snd_pcm_uframes_t *)&bufferSize);

    // /* Set period size to 256 frames. */
    m_frames = 512;
    rc = snd_pcm_hw_params_set_period_size_near(m_playback_handle, m_params, &m_frames, &dir);
    if(rc < 0){
        fprintf(stdout, "snd_pcm_hw_params_set_period_size_near failed: %s\n", snd_strerror(rc) );
    }

    /* Write the parameters to the driver */
    rc = snd_pcm_hw_params(m_playback_handle, m_params);
    if (rc < 0) {
    fprintf(stderr,
            "unable to set hw parameters: %s %d\n",
            snd_strerror(rc), rc);
    exit(1);
    }
    
    //end PCM setup
    m_opened = true;
    return rc;
}

void LinuxAudioPlayer::SetAlsaMasterVolume(long volume)
{
    long min, max;
    snd_mixer_t *handle;
    snd_mixer_selem_id_t *sid;
    const char *card = m_device.c_str();
    const char *selem_name = "Master";

    snd_mixer_open(&handle, 0);
    snd_mixer_attach(handle, card);
    snd_mixer_selem_register(handle, NULL, NULL);
    snd_mixer_load(handle);

    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, selem_name);
    snd_mixer_elem_t* elem = snd_mixer_find_selem(handle, sid);

    snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
    snd_mixer_selem_set_playback_volume_all(elem, volume * max / 100);

    snd_mixer_close(handle);
}

int LinuxAudioPlayer::GetBufferSize(){
    int dir;
    
    /* Use a buffer large enough to hold one period */
    snd_pcm_hw_params_get_period_size(m_params, &m_frames,
                                    &dir);
    int size = m_frames * m_bytesPerSample * m_numChannels; 
    return size;
}

void LinuxAudioPlayer::PlayerThreadMain(){
    m_canceled = false;
    while(m_canceled == false){
        // here we will wait to be woken up since there is no audio left to play
        std::unique_lock<std::mutex> lk{ m_threadMutex };
        m_conditionVariable.wait(lk);
        lk.unlock();
        
        while (m_audioQueue.size() > 0) {
            m_isPlaying = true;
            std::shared_ptr<AudioPlayerEntry> entry = std::make_shared<AudioPlayerEntry>(m_audioQueue.front());
            switch(entry->m_entryType){
                case PlayerEntryType::BYTE_ARRAY:
                    PlayByteBuffer(entry);
                    break;
                case PlayerEntryType::PULL_AUDIO_OUTPUT_STREM:
                    PlayPullAudioOutputStream(entry);
                    break;
                default:
                    fprintf(stderr, "Unknown Audio Player Entry type\n");   
            }
            m_queueMutex.lock();
            m_audioQueue.pop_front();
            m_queueMutex.unlock();
        }
        m_isPlaying = false;
    }
    
}

void LinuxAudioPlayer::PlayPullAudioOutputStream(std::shared_ptr<AudioPlayerEntry> pEntry){
    size_t playBufferSize = GetBufferSize();
    unsigned int bytesRead = 0;
    std::unique_ptr<unsigned char []> playBuffer = std::make_unique<unsigned char[]>(playBufferSize);
    do{
        bytesRead = pEntry->m_pullStream->Read(playBuffer.get(), playBufferSize);
        WriteToALSA(playBuffer.get());
    }while(bytesRead > 0);
}

void LinuxAudioPlayer::PlayByteBuffer(std::shared_ptr<AudioPlayerEntry> pEntry){
    size_t playBufferSize = GetBufferSize();
    size_t bufferLeft = pEntry->m_size;
    std::unique_ptr<unsigned char []> playBuffer = std::make_unique<unsigned char[]>(playBufferSize);
    while(bufferLeft > 0){
        if(bufferLeft >= playBufferSize){
            memcpy(playBuffer.get(), &pEntry->m_data[pEntry->m_size - bufferLeft], playBufferSize);
            bufferLeft -= playBufferSize;
        }else { //there is a smaller amount to play so we will pad with silence
            memcpy(playBuffer.get(), &pEntry->m_data[pEntry->m_size - bufferLeft], bufferLeft);
            memset(playBuffer.get() + bufferLeft, 0, playBufferSize - bufferLeft);
            bufferLeft = 0;
        }
        WriteToALSA(playBuffer.get());
    }
}

int LinuxAudioPlayer::Play(uint8_t* buffer, size_t bufferSize){
    int rc = 0;
    AudioPlayerEntry entry(buffer, bufferSize);
    m_queueMutex.lock();
    m_audioQueue.push_back(entry);
    m_queueMutex.unlock();
    
    if(!m_isPlaying){
        //wake up the audio thread
        m_conditionVariable.notify_one();
    }
    
    return rc;
}

int LinuxAudioPlayer::Play(std::shared_ptr<Microsoft::CognitiveServices::Speech::Audio::PullAudioOutputStream> pStream){
    int rc = 0;
    AudioPlayerEntry entry(pStream);
    m_queueMutex.lock();
    m_audioQueue.push_back(entry);
    m_queueMutex.unlock();
    
    if(!m_isPlaying){
        //wake up the audio thread
        m_conditionVariable.notify_one();
    }
    
    return rc;
}

int LinuxAudioPlayer::WriteToALSA(uint8_t* buffer){
    int rc = 0;
    
    rc = snd_pcm_writei(m_playback_handle, buffer, m_frames);
    if (rc == -EPIPE) {
        /* EPIPE means underrun */
        fprintf(stderr, "underrun occurred\n");
        snd_pcm_prepare(m_playback_handle);
    } else if (rc < 0) {
        fprintf(stderr,
                "error from writei: %s\n",
                snd_strerror(rc));
    }  else if (rc != (int)m_frames) {
        fprintf(stderr, "short write, write %d frames\n", rc);
    }
    
    return rc;
}

int LinuxAudioPlayer::Close(){
    snd_pcm_drain(m_playback_handle);
    snd_pcm_close(m_playback_handle);
    
    return 0;
}