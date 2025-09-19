#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fstream>
#include <assert.h>

#include "audio.h"
#include "precomp.h"

#ifdef _MSC_VER
#pragma warning(disable:4996)
#endif

#if defined(__APPLE__)
#include <string.h>
#else

extern "C" {
#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#endif



using namespace std;

namespace funasr {
// see http://soundfile.sapp.org/doc/WaveFormat/
// Note: We assume little endian here
struct WaveHeader {
};
static_assert(sizeof(WaveHeader) == WAV_HEADER_SIZE, "");

class AudioWindow {
};

AudioFrame::AudioFrame(){}
AudioFrame::AudioFrame(int len) : len(len)
{
    start = 0;
}
AudioFrame::AudioFrame(const AudioFrame &other)
{
    start = other.start;
    end = other.end;
    len = other.len;
    is_final = other.is_final;
}
AudioFrame::AudioFrame(int start, int end, bool is_final):start(start),end(end),is_final(is_final){
    len = end - start;
}
AudioFrame::~AudioFrame(){
    if(data != nullptr){
        free(data);
        data = nullptr;
    }
}
int AudioFrame::SetStart(int val)
{
    start = val < 0 ? 0 : val;
    return start;
}

int AudioFrame::SetEnd(int val)
{
    end = val;
    len = end - start;
    return end;
}

int AudioFrame::GetStart()
{
    return start;
}

int AudioFrame::GetLen()
{
    return len;
}

int AudioFrame::Disp()
{
    LOG(ERROR) << "Not imp!!!!";
    return 0;
}

Audio::Audio(int data_type) : dest_sample_rate(MODEL_SAMPLE_RATE), data_type(data_type)
{
    speech_buff = nullptr;
    speech_data = nullptr;
    align_size = 1360;
    seg_sample = dest_sample_rate / 1000;
}

Audio::Audio(int model_sample_rate, int data_type) : dest_sample_rate(model_sample_rate), data_type(data_type)
{
    speech_buff = nullptr;
    speech_data = nullptr;
    align_size = 1360;
    seg_sample = dest_sample_rate / 1000;
}

Audio::Audio(int model_sample_rate, int data_type, int size) : dest_sample_rate(model_sample_rate), data_type(data_type)
{
    speech_buff = nullptr;
    speech_data = nullptr;
    align_size = (float)size;
    seg_sample = dest_sample_rate / 1000;
}

Audio::~Audio()
{
    if (speech_buff != nullptr) {
        free(speech_buff);
        speech_buff = nullptr;
    }
    if (speech_data != nullptr) {
        free(speech_data);
        speech_data = nullptr;
    }
    if (speech_char != nullptr) {
        free(speech_char);
        speech_char = nullptr;
    }
    {
        std::lock_guard<std::mutex> lockGurad(frame_queue_mutex);
        ClearQueue(frame_queue);
    }
    {
        std::lock_guard<std::mutex> lockGurad(asr_online_queue_mutex);
        ClearQueue(asr_online_queue);
    }
    {
        std::lock_guard<std::mutex> lockGurad(asr_offline_queue_mutex);
        ClearQueue(asr_offline_queue);
    }
}

void Audio::ClearQueue(std::queue<AudioFrame*>& q) {
    while (!q.empty()) {
        AudioFrame* frame = q.front();
        delete frame;
        q.pop();
    }
}

void Audio::Disp()
{
    LOG(INFO) << "Audio time is " << (float)speech_len / dest_sample_rate << " s. len is " << speech_len;
}

float Audio::GetTimeLen()
{
    return (float)speech_len / dest_sample_rate;
}

void Audio::WavResample(int32_t sampling_rate, const float *waveform,
                          int32_t n)
{
    LOG(INFO) << "Creating a resampler: "
              << " in_sample_rate: "<< sampling_rate
              << " output_sample_rate: " << static_cast<int32_t>(dest_sample_rate);
    float min_freq =
        std::min<int32_t>(sampling_rate, dest_sample_rate);
    float lowpass_cutoff = 0.99 * 0.5 * min_freq;

    int32_t lowpass_filter_width = 6;

    auto resampler = std::make_unique<LinearResample>(
          sampling_rate, dest_sample_rate, lowpass_cutoff, lowpass_filter_width);
    std::vector<float> samples;
    resampler->Resample(waveform, n, true, &samples);
    //reset speech_data
    speech_len = samples.size();
    if (speech_data != nullptr) {
        free(speech_data);
        speech_data = nullptr;
    }
    speech_data = (float*)malloc(sizeof(float) * speech_len);
    memset(speech_data, 0, sizeof(float) * speech_len);
    copy(samples.begin(), samples.end(), speech_data);
}

bool Audio::FfmpegLoad(const char *filename, bool copy2char){
}

bool Audio::FfmpegLoad(const char* buf, int n_file_len){
}


bool Audio::LoadWav(const char *filename, int32_t* sampling_rate, bool resample)
}

bool Audio::LoadWav2Char(const char *filename, int32_t* sampling_rate)
{
    WaveHeader header;
    if (speech_char != nullptr) {
        free(speech_char);
        speech_char = nullptr;
    }
    offset = 0;
    std::ifstream is(filename, std::ifstream::binary);
    is.read(reinterpret_cast<char *>(&header), sizeof(header));
    if(!is){
        LOG(ERROR) << "Failed to read " << filename;
        return false;
    }
    if (!header.Validate()) {
        return false;
    }
    header.SeekToDataChunk(is);
        if (!is) {
            return false;
    }
    if (!header.Validate()) {
        return false;
    }
    header.SeekToDataChunk(is);
    if (!is) {
        return false;
    }
    
    *sampling_rate = header.sample_rate;
    // header.subchunk2_size contains the number of bytes in the data.
    // As we assume each sample contains two bytes, so it is divided by 2 here
    speech_len = header.subchunk2_size / 2;
    speech_char = (char *)malloc(header.subchunk2_size);
    memset(speech_char, 0, header.subchunk2_size);
    is.read(speech_char, header.subchunk2_size);

    return true;
}

bool Audio::LoadWav(const char* buf, int n_file_len, int32_t* sampling_rate)
{ 
    std::lock_guard<std::mutex> lockGurad(frame_queue_mutex);
    WaveHeader header;
    if (speech_data != nullptr) {
        free(speech_data);
        speech_data = nullptr;
    }
    if (speech_buff != nullptr) {
        free(speech_buff);
        speech_buff = nullptr;
    }

    std::memcpy(&header, buf, sizeof(header));

    *sampling_rate = header.sample_rate;
    speech_len = header.subchunk2_size / 2;
    speech_buff = (int16_t *)malloc(sizeof(int16_t) * speech_len);
    if (speech_buff)
    {
        memset(speech_buff, 0, sizeof(int16_t) * speech_len);
        memcpy((void*)speech_buff, (const void*)(buf + WAV_HEADER_SIZE), speech_len * sizeof(int16_t));

        speech_data = (float*)malloc(sizeof(float) * speech_len);
        memset(speech_data, 0, sizeof(float) * speech_len);

        float scale = 1;
        if (data_type == 1) {
            scale = 32768;
        }

        for (int32_t i = 0; i != speech_len; ++i) {
            speech_data[i] = (float)speech_buff[i] / scale;
        }
        
        //resample
        if(*sampling_rate != dest_sample_rate){
            WavResample(*sampling_rate, speech_data, speech_len);
        }

        AudioFrame* frame = new AudioFrame(speech_len);
        frame_queue.push(frame);

        return true;
    }
    else
        return false;
}

bool Audio::LoadPcmwav(const char* buf, int n_buf_len, int32_t* sampling_rate)
{
    std::lock_guard<std::mutex> lockGurad(frame_queue_mutex);
    if (speech_data != nullptr) {
        free(speech_data);
        speech_data = nullptr;
    }

    speech_len = n_buf_len / 2;
    speech_data = (float*)malloc(sizeof(float) * speech_len);
    if(speech_data){
        float scale = 1;
        if (data_type == 1) {
            scale = 32768.0f;
        }
        const uint8_t* byte_buf = reinterpret_cast<const uint8_t*>(buf);
        for (int32_t i = 0; i < speech_len; ++i) {
            int16_t val = (int16_t)((byte_buf[2 * i + 1] << 8) | byte_buf[2 * i]);
            speech_data[i] = (float)val / scale;
        }

        //resample
        if(*sampling_rate != dest_sample_rate){
            WavResample(*sampling_rate, speech_data, speech_len);
        }

        AudioFrame* frame = new AudioFrame(speech_len);
        frame_queue.push(frame);
    
        return true;
    }else{
        return false;
    }
}

bool Audio::LoadPcmwavOnline(const char* buf, int n_buf_len, int32_t* sampling_rate)
{
    std::lock_guard<std::mutex> lockGurad(frame_queue_mutex);
    if (speech_data != nullptr) {
        free(speech_data);
        speech_data = nullptr;
    }

    speech_len = n_buf_len / 2;
    speech_data = (float*)malloc(sizeof(float) * speech_len);
    if(speech_data){
        float scale = 1;
        if (data_type == 1) {
            scale = 32768.0f;
        }
        const uint8_t* byte_buf = reinterpret_cast<const uint8_t*>(buf);
        for (int32_t i = 0; i < speech_len; ++i) {
            int16_t val = (int16_t)((byte_buf[2 * i + 1] << 8) | byte_buf[2 * i]);
            speech_data[i] = (float)val / scale;
        }

        //resample
        if(*sampling_rate != dest_sample_rate){
            WavResample(*sampling_rate, speech_data, speech_len);
        }

        for (int32_t i = 0; i != speech_len; ++i) {
            all_samples.emplace_back(speech_data[i]);
        }

        AudioFrame* frame = new AudioFrame(speech_len);
        frame_queue.push(frame);
    
        return true;
    }else{
        return false;
    }
}

bool Audio::LoadPcmwav(const char* filename, int32_t* sampling_rate, bool resample)
{
    std::lock_guard<std::mutex> lockGurad(frame_queue_mutex);
    if (speech_data != nullptr) {
        free(speech_data);
        speech_data = nullptr;
    }
    if (speech_buff != nullptr) {
        free(speech_buff);
        speech_buff = nullptr;
    }
    offset = 0;

    FILE* fp;
    fp = fopen(filename, "rb");
    if (fp == nullptr)
	{
        LOG(ERROR) << "Failed to read " << filename;
        return false;
	}
    fseek(fp, 0, SEEK_END);
    uint32_t n_file_len = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    speech_len = (n_file_len) / 2;
    speech_buff = (int16_t*)malloc(sizeof(int16_t) * speech_len);
    if (speech_buff)
    {
        memset(speech_buff, 0, sizeof(int16_t) * speech_len);
        int ret = fread(speech_buff, sizeof(int16_t), speech_len, fp);
        fclose(fp);

        speech_data = (float*)malloc(sizeof(float) * speech_len);
        memset(speech_data, 0, sizeof(float) * speech_len);

        float scale = 1;
        if (data_type == 1) {
            scale = 32768;
        }
        for (int32_t i = 0; i != speech_len; ++i) {
            speech_data[i] = (float)speech_buff[i] / scale;
        }

        //resample
        if(resample && *sampling_rate != dest_sample_rate){
            WavResample(*sampling_rate, speech_data, speech_len);
        }

        AudioFrame* frame = new AudioFrame(speech_len);
        frame_queue.push(frame);
    
        return true;
    }
    else
        return false;

}

bool Audio::LoadPcmwav2Char(const char* filename, int32_t* sampling_rate)
{
    if (speech_char != nullptr) {
        free(speech_char);
        speech_char = nullptr;
    }
    offset = 0;

    FILE* fp;
    fp = fopen(filename, "rb");
    if (fp == nullptr)
	{
        LOG(ERROR) << "Failed to read " << filename;
        return false;
	}
    fseek(fp, 0, SEEK_END);
    uint32_t n_file_len = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    speech_len = (n_file_len) / 2;
    speech_char = (char *)malloc(n_file_len);
    memset(speech_char, 0, n_file_len);
    fread(speech_char, sizeof(int16_t), n_file_len/2, fp);
    fclose(fp);
    
    return true;
}

bool Audio::LoadOthers2Char(const char* filename)
{
    if (speech_char != nullptr) {
        free(speech_char);
        speech_char = nullptr;
    }

    FILE* fp;
    fp = fopen(filename, "rb");
    if (fp == nullptr)
	{
        LOG(ERROR) << "Failed to read " << filename;
        return false;
	}
    fseek(fp, 0, SEEK_END);
    uint32_t n_file_len = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    speech_len = n_file_len;
    speech_char = (char *)malloc(n_file_len);
    memset(speech_char, 0, n_file_len);
    fread(speech_char, 1, n_file_len, fp);
    fclose(fp);
    
    return true;
}

int Audio::FetchTpass(AudioFrame *&frame)
{
    std::lock_guard<std::mutex> lockGurad(asr_offline_queue_mutex);
    if (asr_offline_queue.size() > 0) {
        frame = asr_offline_queue.front();
        asr_offline_queue.pop();
        return 1;
    } else {
        return 0;
    }
}

int Audio::FetchChunck(AudioFrame *&frame)
{
    std::lock_guard<std::mutex> lockGurad(asr_online_queue_mutex);
    if (asr_online_queue.size() > 0) {
        frame = asr_online_queue.front();
        asr_online_queue.pop();
        return 1;
    } else {
        return 0;
    }
}

int Audio::Fetch(float *&dout, int &len, int &flag)
{
    std::lock_guard<std::mutex> lockGurad(frame_queue_mutex);
    if (frame_queue.size() > 0) {
        AudioFrame *frame = frame_queue.front();
        frame_queue.pop();

        dout = speech_data + frame->GetStart();
        len = frame->GetLen();
        delete frame;
        flag = S_END;
        return 1;
    } else {
        return 0;
    }
}

int Audio::Fetch(float *&dout, int &len, int &flag, float &start_time)
{
    std::lock_guard<std::mutex> lockGurad(frame_queue_mutex);
    if (frame_queue.size() > 0) {
        AudioFrame *frame = frame_queue.front();
        frame_queue.pop();

        start_time = (float)(frame->GetStart())/ dest_sample_rate;
        dout = speech_data + frame->GetStart();
        len = frame->GetLen();
        delete frame;
        flag = S_END;
        return 1;
    } else {
        return 0;
    }
}

int Audio::Fetch(float**& dout, int*& len, int*& flag, float*& start_time, int batch_size, int &batch_in)
{
    std::lock_guard<std::mutex> lockGurad(frame_queue_mutex);
    batch_in = std::min((int)frame_queue.size(), batch_size);
    if (batch_in == 0){
        return 0;
    } else{
        // init
        dout = new float*[batch_in];
        len = new int[batch_in];
        flag = new int[batch_in];
        start_time = new float[batch_in];

        for(int idx=0; idx < batch_in; idx++){
            AudioFrame *frame = frame_queue.front();
            frame_queue.pop();

            start_time[idx] = (float)(frame->GetStart())/ dest_sample_rate;
            dout[idx] = speech_data + frame->GetStart();
            len[idx] = frame->GetLen();
            delete frame;
            flag[idx] = S_END;
        }
        return 1;
    }
}

int Audio::FetchDynamic(float**& dout, int*& len, int*& flag, float*& start_time, int batch_size, int &batch_in)
{
    std::lock_guard<std::mutex> lockGurad(frame_queue_mutex);
    //compute batch size
    queue<AudioFrame *> frame_batch;
    int max_acc = 300*1000*seg_sample;
    int max_sent = 60*1000*seg_sample;
    int bs_acc = 0;
    int max_len = 0;
    int max_batch = 1;
    #ifdef USE_GPU
        max_batch = batch_size;
    #endif
    max_batch = std::min(max_batch, (int)frame_queue.size());

    for(int idx=0; idx < max_batch; idx++){
        AudioFrame *frame = frame_queue.front();
        int length = frame->GetLen();
        if(length >= max_sent){
            if(bs_acc == 0){
                bs_acc++;
                frame_batch.push(frame);
                frame_queue.pop();                
            }
            break;
        }
        max_len = std::max(max_len, frame->GetLen());
        if(max_len*(bs_acc+1) > max_acc){
            break;
        }
        bs_acc++;
        frame_batch.push(frame);
        frame_queue.pop();
    }

    batch_in = (int)frame_batch.size();
    if (batch_in == 0){
        return 0;
    } else{
        // init
        dout = new float*[batch_in];
        len = new int[batch_in];
        flag = new int[batch_in];
        start_time = new float[batch_in];

        for(int idx=0; idx < batch_in; idx++){
            AudioFrame *frame = frame_batch.front();
            frame_batch.pop();

            start_time[idx] = (float)(frame->GetStart())/ dest_sample_rate;
            dout[idx] = speech_data + frame->GetStart();
            len[idx] = frame->GetLen();
            delete frame;
            flag[idx] = S_END;
        }
        return 1;
    }
}

void Audio::Padding()
{
    std::lock_guard<std::mutex> lockGurad(frame_queue_mutex);
    float num_samples = speech_len;
    float frame_length = 400;
    float frame_shift = 160;
    float num_frames = floor((num_samples + (frame_shift / 2)) / frame_shift);
    float num_new_samples = (num_frames - 1) * frame_shift + frame_length;
    float num_padding = num_new_samples - num_samples;
    float num_left_padding = (frame_length - frame_shift) / 2;
    float num_right_padding = num_padding - num_left_padding;

    float *new_data = (float *)malloc(num_new_samples * sizeof(float));
    int i;
    int tmp_off = 0;
    for (i = 0; i < num_left_padding; i++) {
        int ii = num_left_padding - i - 1;
        new_data[i] = speech_data[ii];
    }
    tmp_off = num_left_padding;
    memcpy(new_data + tmp_off, speech_data, speech_len * sizeof(float));
    tmp_off += speech_len;

    for (i = 0; i < num_right_padding; i++) {
        int ii = speech_len - i - 1;
        new_data[tmp_off + i] = speech_data[ii];
    }
    free(speech_data);
    speech_data = nullptr;
    speech_data = new_data;
    speech_len = num_new_samples;

    AudioFrame *frame = new AudioFrame(num_new_samples);
    frame_queue.push(frame);
    frame = frame_queue.front();
    frame_queue.pop();
    delete frame;
}

void Audio::Split(OfflineStream* offline_stream)
{
    std::lock_guard<std::mutex> lockGurad(frame_queue_mutex);
    AudioFrame *frame;

    frame = frame_queue.front();
    frame_queue.pop();
    int sp_len = frame->GetLen();
    delete frame;
    frame = nullptr;

    std::vector<float> pcm_data(speech_data, speech_data+sp_len);
    vector<std::vector<int>> vad_segments = (offline_stream->vad_handle)->Infer(pcm_data);
    for(vector<int> segment:vad_segments)
    {
        frame = new AudioFrame();
        int start = segment[0]*seg_sample;
        int end = segment[1]*seg_sample;
        frame->SetStart(start);
        frame->SetEnd(end);
        frame_queue.push(frame);
        frame = nullptr;
    }
}

void Audio::CutSplit(OfflineStream* offline_stream, std::vector<int> &index_vector)
{
    std::lock_guard<std::mutex> lockGurad(frame_queue_mutex);
    std::unique_ptr<VadModel> vad_online_handle = make_unique<FsmnVadOnline>((FsmnVad*)(offline_stream->vad_handle).get());
    AudioFrame *frame;

    frame = frame_queue.front();
    frame_queue.pop();
    int sp_len = frame->GetLen();
    delete frame;
    frame = nullptr;

    int step = dest_sample_rate*1;
    bool is_final=false;
    vector<std::vector<int>> vad_segments;
    for (int sample_offset = 0; sample_offset < speech_len; sample_offset += std::min(step, speech_len - sample_offset)) {
        if (sample_offset + step >= speech_len - 1) {
                step = speech_len - sample_offset;
                is_final = true;
            } else {
                is_final = false;
        }
        std::vector<float> pcm_data(speech_data+sample_offset, speech_data+sample_offset+step);
        vector<std::vector<int>> cut_segments = vad_online_handle->Infer(pcm_data, is_final);
        vad_segments.insert(vad_segments.end(), cut_segments.begin(), cut_segments.end());
    }    

    int speech_start_i = -1, speech_end_i =-1;
    std::vector<AudioFrame*> vad_frames;
    for(vector<int> vad_segment:vad_segments)
    {
        if(vad_segment.size() != 2){
            LOG(ERROR) << "Size of vad_segment is not 2.";
            break;
        }
        if(vad_segment[0] != -1){
            speech_start_i = vad_segment[0];
        }
        if(vad_segment[1] != -1){
            speech_end_i = vad_segment[1];
        }

        if(speech_start_i!=-1 && speech_end_i!=-1){
            int start = speech_start_i*seg_sample;
            int end = speech_end_i*seg_sample;
            frame = new AudioFrame(end-start);
            frame->SetStart(start);
            frame->SetEnd(end);
            vad_frames.push_back(frame);
            frame = nullptr;
            speech_start_i=-1;
            speech_end_i=-1;
        }

    }
    // sort
    {
        index_vector.clear();
        index_vector.resize(vad_frames.size());
        for (int i = 0; i < index_vector.size(); ++i) {
            index_vector[i] = i;
        }
        std::sort(index_vector.begin(), index_vector.end(), [&vad_frames](const int a, const int b) {
            return vad_frames[a]->len < vad_frames[b]->len;
        });
        for (int idx : index_vector) {
            frame_queue.push(vad_frames[idx]);
        }
    }
}

void Audio::Split(VadModel* vad_obj, vector<std::vector<int>>& vad_segments, bool input_finished)
{
    std::lock_guard<std::mutex> lockGurad(frame_queue_mutex);
    AudioFrame *frame;

    frame = frame_queue.front();
    frame_queue.pop();
    int sp_len = frame->GetLen();
    delete frame;
    frame = nullptr;

    std::vector<float> pcm_data(speech_data, speech_data+sp_len);
    vad_segments = vad_obj->Infer(pcm_data, input_finished);
}

// 2pass
void Audio::Split(VadModel* vad_obj, int chunk_len, bool input_finished, ASR_TYPE asr_mode)
{
    AudioFrame *frame;

    {
        std::lock_guard<std::mutex> lockGurad(frame_queue_mutex);
        frame = frame_queue.front();
        frame_queue.pop();
        int sp_len = frame->GetLen();
        delete frame;
        frame = nullptr;
    }

    std::vector<float> pcm_data(speech_data, speech_data+sp_len);
    vector<std::vector<int>> vad_segments = vad_obj->Infer(pcm_data, input_finished);

    speech_end += sp_len/seg_sample;
    if(vad_segments.size() == 0){
        if(speech_start != -1){
            int start = speech_start*seg_sample;
            int end = speech_end*seg_sample;
            int buff_len = end-start;
            int step = chunk_len;

            if(asr_mode != ASR_OFFLINE){
                if(buff_len >= step){
                    std::lock_guard<std::mutex> lockGurad(asr_online_queue_mutex);
                    frame = new AudioFrame(step);
                    frame->global_start = speech_start;
                    frame->global_end = speech_start + step/seg_sample;
                    frame->data = (float*)malloc(sizeof(float) * step);
                    memcpy(frame->data, all_samples.data()+start-offset, step*sizeof(float));
                    asr_online_queue.push(frame);
                    frame = nullptr;
                    speech_start += step/seg_sample;
                }
            }
        }
    }else{
        for(auto vad_segment: vad_segments){
            int speech_start_i=-1, speech_end_i=-1;
            if(vad_segment[0] != -1){
                speech_start_i = vad_segment[0];
            }
            if(vad_segment[1] != -1){
                speech_end_i = vad_segment[1];
            }

            // [1, 100]
            if(speech_start_i != -1 && speech_end_i != -1){
                int start = speech_start_i*seg_sample;
                int end = speech_end_i*seg_sample;

                if(asr_mode != ASR_OFFLINE){
                    std::lock_guard<std::mutex> lockGurad(asr_online_queue_mutex);
                    frame = new AudioFrame(end-start);
                    frame->is_final = true;
                    frame->global_start = speech_start_i;
                    frame->global_end = speech_end_i;
                    frame->data = (float*)malloc(sizeof(float) * (end-start));
                    memcpy(frame->data, all_samples.data()+start-offset, (end-start)*sizeof(float));
                    asr_online_queue.push(frame);
                    frame = nullptr;
                }

                if(asr_mode != ASR_ONLINE){
                    std::lock_guard<std::mutex> lockGurad(asr_offline_queue_mutex);
                    frame = new AudioFrame(end-start);
                    frame->is_final = true;
                    frame->global_start = speech_start_i;
                    frame->global_end = speech_end_i;
                    frame->data = (float*)malloc(sizeof(float) * (end-start));
                    memcpy(frame->data, all_samples.data()+start-offset, (end-start)*sizeof(float));
                    asr_offline_queue.push(frame);
                    frame = nullptr;
                }

                speech_start = -1;
                speech_offline_start = -1;
            // [70, -1]
            }else if(speech_start_i != -1){
                speech_start = speech_start_i;
                speech_offline_start = speech_start_i;
                
                int start = speech_start*seg_sample;
                int end = speech_end*seg_sample;
                int buff_len = end-start;
                int step = chunk_len;

                if(asr_mode != ASR_OFFLINE){
                    if(buff_len >= step){
                        std::lock_guard<std::mutex> lockGurad(asr_online_queue_mutex);
                        frame = new AudioFrame(step);
                        frame->global_start = speech_start;
                        frame->global_end = speech_start + step/seg_sample;
                        frame->data = (float*)malloc(sizeof(float) * step);
                        memcpy(frame->data, all_samples.data()+start-offset, step*sizeof(float));
                        asr_online_queue.push(frame);
                        frame = nullptr;
                        speech_start += step/seg_sample;
                    }
                }

            }else if(speech_end_i != -1){ // [-1,100]
                if(speech_start == -1 || speech_offline_start == -1){
                    LOG(ERROR) <<"Vad start is null while vad end is available. Set vad start 0" ;
                    speech_start = 0;
                }

                int start = speech_start*seg_sample;
                int offline_start = speech_offline_start*seg_sample;
                int end = speech_end_i*seg_sample;
                int buff_len = end-start;
                int step = chunk_len;

                if(asr_mode != ASR_ONLINE){
                    std::lock_guard<std::mutex> lockGurad(asr_offline_queue_mutex);
                    frame = new AudioFrame(end-offline_start);
                    frame->is_final = true;
                    frame->global_start = speech_offline_start;
                    frame->global_end = speech_end_i;
                    frame->data = (float*)malloc(sizeof(float) * (end-offline_start));
                    memcpy(frame->data, all_samples.data()+offline_start-offset, (end-offline_start)*sizeof(float));
                    asr_offline_queue.push(frame);
                    frame = nullptr;
                }

                if(asr_mode != ASR_OFFLINE){
                    if(buff_len > 0){
                        std::lock_guard<std::mutex> lockGurad(asr_online_queue_mutex);
                        for (int sample_offset = 0; sample_offset < buff_len; sample_offset += std::min(step, buff_len - sample_offset)) {
                            bool is_final = false;
                            if (sample_offset + step >= buff_len - 1) {
                                step = buff_len - sample_offset;
                                is_final = true;
                            }
                            frame = new AudioFrame(step);
                            frame->is_final = is_final;
                            frame->global_start = (int)((start+sample_offset)/seg_sample);
                            frame->global_end = frame->global_start + step/seg_sample;
                            frame->data = (float*)malloc(sizeof(float) * step);
                            memcpy(frame->data, all_samples.data()+start-offset+sample_offset, step*sizeof(float));
                            asr_online_queue.push(frame);
                            frame = nullptr;
                        }
                    }else{
                        std::lock_guard<std::mutex> lockGurad(asr_online_queue_mutex);
                        frame = new AudioFrame(0);
                        frame->is_final = true;
                        frame->global_start = speech_start;   // in this case start >= end
                        frame->global_end = speech_end_i;
                        asr_online_queue.push(frame);
                        frame = nullptr;
                    }
                }
                speech_start = -1;
                speech_offline_start = -1;
            }
        }
    }

    // erase all_samples
    int vector_cache = dest_sample_rate*2;
    if(speech_offline_start == -1){
        if(all_samples.size() > vector_cache){
            int erase_num = all_samples.size() - vector_cache;
            all_samples.erase(all_samples.begin(), all_samples.begin()+erase_num);
            offset += erase_num;
        }
    }else{
        int offline_start = speech_offline_start*seg_sample;
         if(offline_start-offset > vector_cache){
            int erase_num = offline_start-offset - vector_cache;
            all_samples.erase(all_samples.begin(), all_samples.begin()+erase_num);
            offset += erase_num;
        }       
    }
    
}

} // namespace funasr