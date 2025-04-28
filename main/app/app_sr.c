#include "app_sr.h"
#include "string.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_afe_sr_models.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "model_path.h"
#include "audio.h"
#include "app_stt.h"
#include "app_llm.h"
#include "app_tts.h"

static const char *TAG = "app_sr";

// 全局变量，用于音频录制和播放状态
static size_t               g_stt_recorded_length = 0;    ///< STT录制的音频长度
static bool                 g_voice_recording = false;      ///< 标志，表示语音录制是否激活
static bool                 g_audio_playing = false;        ///< 标志，表示音频播放是否激活
static bool                 g_face_updated = false;         ///< 标志，用于UI面部更新（可能与UI相关，保持原样）

// 音频处理任务的任务句柄
static TaskHandle_t         xFeedHandle = NULL;             ///< 音频馈送任务句柄
static TaskHandle_t         xDetectHandle = NULL;           ///< 音频检测任务句柄

// 互任务通信的队列
static QueueHandle_t        g_audio_chat_queue = NULL;      ///< 发送文本到聊天任务的队列
static QueueHandle_t        g_audio_tts_queue = NULL;       ///< 发送文本到TTS任务的队列
static QueueHandle_t        g_queue_audio_play = NULL;      ///< 发送音频数据到播放任务的队列
static QueueHandle_t        g_result_que = NULL;            ///< 接收音频录制状态结果的队列
static EventGroupHandle_t   g_stt_event_group = NULL;       ///< STT任务同步的事件组

// 外部和静态编解码器设备句柄
extern esp_codec_dev_handle_t   audio_codec_dev;            ///< 外部音频编解码器设备句柄
static esp_codec_dev_handle_t   spk_codec_dev = NULL;       ///< 扬声器编解码器设备句柄
static esp_codec_dev_handle_t   mic_codec_dev = NULL;       ///< 麦克风编解码器设备句柄

// AFE-SR相关句柄和缓冲区
static srmodel_list_t *models = NULL;                      ///< 语音识别模型列表
static int16_t *g_audio_record_buf = NULL;                ///< 音频录制缓冲区
static esp_afe_sr_data_t *afe_data = NULL;                ///< AFE-SR数据结构
static esp_afe_sr_iface_t *afe_handle = NULL;              ///< AFE-SR接口句柄

uint8_t g_running_mode = 0;                                ///< 运行模式（例如，聊天模式）
UBaseType_t high_water_mark;

/**
 * @brief Audio feed task to continuously read audio data from codec and feed to AFE-SR.
 *
 * @param pvParam Pointer to AFE-SR data structure.
 */
static void audio_feed_task(void *pvParam)
{
    esp_afe_sr_data_t *afe_data_local = (esp_afe_sr_data_t *)pvParam; // Local copy for clarity
    int audio_chunksize = afe_handle->get_feed_chunksize(afe_data_local);
    ESP_LOGI(TAG, "audio_chunksize=%d, feed_channel=%d", audio_chunksize, TOTAL_NUM);

    // Allocate audio buffer once and reuse it
    int16_t *audio_buffer = heap_caps_malloc(audio_chunksize * sizeof(int16_t) * TOTAL_NUM, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (NULL == audio_buffer) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        vTaskDelete(NULL); // Task should exit if allocation fails
        return;
    }
    mic_codec_dev = audio_codec_dev;

    while (true) {
        // Read audio data from codec device
        if (esp_codec_dev_read(mic_codec_dev, audio_buffer, audio_chunksize * I2S_CHANNEL_NUM * sizeof(int16_t)) != ESP_OK) {
            ESP_LOGE(TAG, "Error reading from codec device in feed task");
            continue; // Continue to the next iteration, avoid task crash
        }
        // Process audio data if voice recording is active
        if (g_voice_recording) {
            // Stop recording if max recording length is reached
            if (g_stt_recorded_length >= MAX_AUDIO_INPUT_LENGTH) {
                audio_record_state_t result = AUDIO_VAD_END;
                if (xQueueSend(g_result_que, &result, 10) != pdTRUE) {
                    ESP_LOGW(TAG, "Failed to send AUDIO_VAD_END to result queue");
                }
                g_voice_recording = false; // Stop future recording
                continue;                  // Skip writing more data for current chunk
            }

            /// 考虑最大记录长度，计算要写入的字节数
            size_t bytes_to_write = audio_chunksize * sizeof(int16_t) * I2S_CHANNEL_NUM;
            if (g_stt_recorded_length + bytes_to_write > MAX_AUDIO_INPUT_LENGTH) {
                bytes_to_write = MAX_AUDIO_INPUT_LENGTH - g_stt_recorded_length;
            }

            // Write audio data to record buffer
            if (g_stt_recorded_length + bytes_to_write <= MAX_AUDIO_INPUT_LENGTH && bytes_to_write > 0) { // Add check for bytes_to_write > 0
                memcpy(g_audio_record_buf + g_stt_recorded_length / 2, audio_buffer, bytes_to_write); // Divide by 2 because g_audio_record_buf is int16_t? Check this.
                g_stt_recorded_length += bytes_to_write;
                // ESP_LOGV(TAG, "Recording: %d bytes written, total: %d", bytes_to_write, g_stt_recorded_length);
            }
            else if (bytes_to_write <= 0) {
                ESP_LOGW(TAG, "No bytes to write, buffer might be full or recording ended");
            }
            else {
                ESP_LOGW(TAG, "Buffer full, unable to write more audio data");
                audio_record_state_t result = AUDIO_VAD_END;
                if (xQueueSend(g_result_que, &result, 10) != pdTRUE) {
                    ESP_LOGW(TAG, "Failed to send AUDIO_VAD_END to result queue");
                }
                g_voice_recording = false; // Stop future recording
            }
        }

        // Channel Adjust - Mono to Stereo conversion (Left channel duplicated, right channel zeroed)
        for (int i = audio_chunksize - 1; i >= 0; i--) {
            audio_buffer[i * 2 + 1] = 0;         // Right channel zeroed
            audio_buffer[i * 2 + 0] = audio_buffer[i]; // Left channel duplicated from mono
        }

        // Feed audio samples to AFE_SR for processing
        afe_handle->feed(afe_data_local, audio_buffer);
    }
    // Task will never return in normal operation. Memory allocated for `audio_buffer` is held for the task's lifetime.
    // If task deletion is possible in future for error handling/cleanup, free `audio_buffer` here.
    // heap_caps_free(audio_buffer); // If task exit path is added later
    // afe_handle->destroy(afe_data_local); // Destroying afe_data here is likely incorrect because other tasks also use it. Destroy in deinit if needed.
    vTaskDelete(NULL); // Should not reach here in normal continuous operation
}

/**
 * @brief Audio detect task to process fetched audio data from AFE-SR for wake word and VAD detection.
 *
 * @param pvParam Pointer to AFE-SR data structure.
 */
static void audio_detect_task(void *pvParam)
{
    bool wait_speech_flag = false;
    bool detect_flag = false;
    vad_state_t vad_state = VAD_SILENCE;
    esp_afe_sr_data_t *afe_data_local = (esp_afe_sr_data_t *)pvParam; // Local copy for clarity

    int afe_chunksize = afe_handle->get_fetch_chunksize(afe_data_local);
    ESP_LOGI(TAG, "------------detect start------------\n");
    ESP_LOGI(TAG, "afe_chunksize: %d", afe_chunksize);

    while (true) {
        afe_fetch_result_t *res = afe_handle->fetch(afe_data_local);
        if (!res || res->ret_value == ESP_FAIL) {
            ESP_LOGE(TAG, "AFE fetch error!");
            continue; // Continue to next iteration upon error
        }
        vad_state = res->vad_state;
        // Wake word detection
        if (res->wakeup_state == WAKENET_DETECTED) {
            // ESP_LOGI(TAG, LOG_BOLD(LOG_COLOR_GREEN) "Wakeword detected");
            audio_record_state_t result = AUDIO_WAKENET_START;
            if (xQueueSend(g_result_que, &result, 10) != pdTRUE) {
                ESP_LOGW(TAG, "Failed to send AUDIO_WAKENET_START to result queue");
            }
            // ui_send_sys_event(ui_face, LV_EVENT_FACE_ASK, NULL); // UI event, keep as is
        }
        else if (res->wakeup_state == WAKENET_CHANNEL_VERIFIED) {
            //ESP_LOGI(TAG, LOG_BOLD(LOG_COLOR_GREEN) "Channel verified");
            afe_handle->disable_wakenet(afe_data_local); // Disable wake word to wait for speech
            wait_speech_flag = true;                     // Set flag to wait for speech
        }

        // VAD Speech detection
        if (wait_speech_flag) {
            if (vad_state == VAD_SPEECH) {
                ESP_LOGI(TAG, "Speech detected, VAD start");
                detect_flag = true;         // Start general detection
                wait_speech_flag = false;    // No longer waiting for speech start, now waiting for speech end
                audio_record_state_t result = AUDIO_VAD_START;
                if (xQueueSend(g_result_que, &result, 10) != pdTRUE) {
                    ESP_LOGW(TAG, "Failed to send AUDIO_VAD_START to result queue");
                }
            }
        }

        // VAD Silence detection (end of speech)
        if (detect_flag) {
            if (vad_state == VAD_SILENCE) {
                ESP_LOGI(TAG, "Waiting for end of speech...");

                // Wait and check for silence for a short duration (e.g., 2 seconds, adjustable)
                for (int i = 0; i < 20; i++) { // 20 iterations * 100ms = 2 seconds
                    vTaskDelay(pdMS_TO_TICKS(100));
                    res = afe_handle->fetch(afe_data_local); // Refetch afe result
                    if (!res) break; // Handle null res if fetch fails during delay.
                    vad_state = res->vad_state;

                    if (vad_state != VAD_SILENCE) {
                        ESP_LOGI(TAG, "Speech continues, extending silence wait.");
                        break; // Exit delay loop if speech continues
                    }
                }

                if (!res) {
                    ESP_LOGE(TAG, "AFE fetch error during silence wait.");
                    continue; // Error fetching during silence check, restart detection loop.
                }
                vad_state = res->vad_state; // Update vad_state after delay loop, even if break out of loop
                if (vad_state == VAD_SILENCE) { // Double check vad_state again after delay
                    ESP_LOGI(TAG, "VAD state: VAD_SILENCE - End of speech detected");
                    audio_record_state_t result = AUDIO_VAD_END;
                    if (xQueueSend(g_result_que, &result, 10) != pdTRUE) {
                        ESP_LOGW(TAG, "Failed to send AUDIO_VAD_END to result queue");
                    }
                    // ui_send_sys_event(ui_face, LV_EVENT_FACE_THINK, NULL); // UI event, keep as is

                    afe_handle->enable_wakenet(afe_data_local); // Re-enable wake word detection
                    detect_flag = false;                        // Reset detect flag for next detection cycle
                }
                else {
                    ESP_LOGI(TAG, "Speech re-detected after short silence. Continuing detection.");
                    // Remain in detect_flag = true state to continue monitoring speech.
                }
                continue; // Continue detection loop
            }
        }
    }
    // Task will never return in normal operation.
    // afe_handle->destroy(afe_data_local); // Destroying afe_data here is likely incorrect. Destroy in deinit if needed.
    vTaskDelete(NULL); // Should not reach here in normal continuous operation
}

/**
 * @brief Audio record task to handle audio recording states and trigger STT processing.
 *
 * @param pvParam Queue handle for receiving audio record state results.
 */
void audio_record_task(void *pvParam)
{
    QueueHandle_t result_queue = (QueueHandle_t)pvParam; // Local queue handle for clarity

    while (true) {
        audio_record_state_t result;
        if (xQueueReceive(result_queue, &result, portMAX_DELAY) == pdTRUE) {
            switch (result) {
                high_water_mark = uxTaskGetStackHighWaterMark(NULL);
                ESP_LOGI("audio_record_task   xxxxxxxxxxxxxxx", "Stack high water mark: %u bytes remaining", high_water_mark * sizeof(StackType_t));
            case AUDIO_PLAY_MUYU: // Unused case, might be placeholder or legacy
                g_audio_playing = false;
                break;
            case AUDIO_WAKENET_START:
                ESP_LOGI(TAG, "Wake word detected - start interaction");
                g_audio_playing = false;
                break;
            case AUDIO_WAKENET_END: // Unused case, might be timeout related, currently logs "timeout"
                ESP_LOGI(TAG, "Wake word detection timeout");
                break;
            case AUDIO_VAD_START:
                ESP_LOGI(TAG, "VAD start - voice activity detected, recording started");
                g_voice_recording = true;
                g_stt_recorded_length = 0; // Reset recorded length at start of recording
                break;
            case AUDIO_VAD_END:
                ESP_LOGI(TAG, "VAD end - voice activity ended, recording stopped");
                g_voice_recording = false;
                if (g_stt_recorded_length > MIN_AUDIO_INPUT_LENGTH) {
                    ESP_LOGI(TAG, "Sufficient audio recorded, triggering STT");
                    xEventGroupSetBits(g_stt_event_group, AUDIO_CHAT_BIT); // Set event to trigger STT task
                }
                else {
                    ESP_LOGI(TAG, "Insufficient audio recorded, STT not triggered");
                }
                break;
            default:
                ESP_LOGW(TAG, "Unknown audio record state: %d", result);
                break;
            }
        }
    }
    vTaskDelete(NULL); // Should not reach here in normal continuous operation
}

/**
 * @brief Application STT task to process recorded audio and send to STT service.
 *
 * @param arg Unused argument.
 */
void app_stt_task(void *arg)
{
    ESP_LOGI(TAG, "app_stt_task start");

    while (1) {
        // Wait for AUDIO_CHAT_BIT event to be set by audio_record_task
        xEventGroupWaitBits(g_stt_event_group, AUDIO_CHAT_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
        ESP_LOGI(TAG, "STT task triggered");
        high_water_mark = uxTaskGetStackHighWaterMark(NULL);
        ESP_LOGI("app_stt_task   xxxxxxxxxxxxxxx", "Stack high water mark: %u bytes remaining", high_water_mark * sizeof(StackType_t));
        // Send recorded audio to Baidu ASR service for speech-to-text conversion
        baidu_asr_send_audio(g_audio_record_buf, g_stt_recorded_length, g_stt_recorded_length); // Length passed twice, needs review, maybe data_len and frame_len?
        g_stt_recorded_length = 0; // Reset recorded length after sending for STT

        // Receive text result from STT service
        char *message_content = NULL;
        baidu_asr_recv_text(&message_content);

        if (message_content == NULL) {
            ESP_LOGE(TAG, "STT returned NULL message content");
            //ui_send_sys_event(ui_face, LV_EVENT_FACE_LOOK, NULL); // UI event, keep as is
            continue; // Continue to next iteration if STT fails
        }
        ESP_LOGI(TAG, "STT result: %s", message_content);

        QueueHandle_t target_queue = NULL;
        if (g_running_mode == MODE_CHAT) {
            target_queue = g_audio_chat_queue; // Send to chat queue if in chat mode
        }
        else {
            ESP_LOGW(TAG, "No queue defined for current running mode, defaulting to chat queue."); // Default to chat queue or handle differently if no chat mode.
            target_queue = g_audio_chat_queue; // Default to chat queue if no mode is matched. Needs better handling.
        }

        // Send STT result to the appropriate queue (e.g., chat queue)
        if (xQueueSend(target_queue, &message_content, 0) == pdFALSE) {
            ESP_LOGE(TAG, "Failed to send STT message to chat queue, freeing message content");
            free(message_content); // Free message content if queue send fails to prevent leak
            message_content = NULL;
        }

        // xEventGroupSetBits(g_stt_event_group, AUDIO_STOP_BIT); // AUDIO_STOP_BIT is set but not used. Remove or repurpose if needed.
    }

    ESP_LOGI(TAG, "app_stt_task end"); // Should not reach here in normal continuous operation
    vTaskDelete(NULL);
}

/**
 * @brief Audio chat task to process text input, send to LLM, and enqueue TTS request.
 *
 * @param arg Unused argument.
 */
void audio_chat_task(void *arg)
{
    ESP_LOGI(TAG, "audio_chat_task start");
    char *chat_data;

    while (xQueueReceive(g_audio_chat_queue, &chat_data, portMAX_DELAY) == pdTRUE) {
        ESP_LOGI(TAG, "Received chat request: %s", chat_data);
        high_water_mark = uxTaskGetStackHighWaterMark(NULL);
        ESP_LOGI("audio_chat_task   xxxxxxxxxxxxxxx", "Stack high water mark: %u bytes remaining", high_water_mark * sizeof(StackType_t));
        // Send chat request to LLM service for response
        llm_send_request(chat_data);
        free(chat_data); // Free chat request data after sending to LLM
        chat_data = NULL;

        char *response_data = NULL;
        esp_err_t ret = llm_recv_response(&response_data); // Receive response from LLM

        if (ret != ESP_OK || response_data == NULL) {
            ESP_LOGE(TAG, "Error receiving response from LLM or response is NULL");
            //ui_send_sys_event(ui_face, LV_EVENT_FACE_LOOK, NULL); // UI event, keep as is
            continue; // Continue to next iteration if LLM fails
        }

        ESP_LOGI(TAG, "LLM response: %s", response_data);

        QueueHandle_t target_queue = NULL;
        if (g_running_mode == MODE_CHAT) {
            target_queue = g_audio_tts_queue; // Send to TTS queue in chat mode
        }
        else {
            ESP_LOGW(TAG, "No TTS queue defined for current running mode, defaulting to TTS queue."); // Defaulting to TTS, handle mode logic better.
            target_queue = g_audio_tts_queue;     // Default to TTS queue, needs mode specific handling if other modes than chat exist.
        }

        // Send LLM response to TTS queue for text-to-speech conversion
        if (xQueueSend(target_queue, &response_data, 0) == pdFALSE) {
            ESP_LOGE(TAG, "Failed to send LLM response to TTS queue, freeing response");
            free(response_data); // Free response data if queue send fails
            response_data = NULL;
        }
    }

    ESP_LOGI(TAG, "audio_chat_task end"); // Should not reach here in normal continuous operation
    vTaskDelete(NULL);
}

/**
 * @brief Enqueue audio data for playback.
 *
 * @param wav Pointer to WAV audio data.
 * @param len Length of audio data in bytes.
 * @return esp_err_t ESP_OK if audio enqueued successfully, ESP_FAIL otherwise.
 */
esp_err_t audio_play(uint8_t *wav, size_t len)
{
    if (!wav || len == 0) {
        ESP_LOGE(TAG, "Invalid audio play parameters: wav=%p, len=%d", wav, len);
        return ESP_ERR_INVALID_ARG;
    }

    audio_data_t audio_data = {
        .len = len,
        .wav = heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT), // Allocate memory for audio data
    };

    if (audio_data.wav == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for audio data");
        return ESP_FAIL;
    }

    memcpy(audio_data.wav, wav, len); // Copy audio data to allocated buffer

    if (xQueueSend(g_queue_audio_play, &audio_data, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to enqueue audio data for playback");
        heap_caps_free(audio_data.wav); // Free allocated memory if queue send fails
        return ESP_FAIL;
    }
    ESP_LOGV(TAG, "Audio data enqueued for playback, len=%d", len);
    return ESP_OK;
}

/**
 * @brief Audio TTS task to process text requests, get audio from TTS service, and enqueue for playback.
 *
 * @param arg Unused argument.
 */
void audio_tts_task(void *arg)
{
    ESP_LOGI(TAG, "audio_tts_task start");
    char *text = NULL;

    while (xQueueReceive(g_audio_tts_queue, &text, portMAX_DELAY) == pdTRUE) {
        high_water_mark = uxTaskGetStackHighWaterMark(NULL);
        ESP_LOGI("audio_tts_task   xxxxxxxxxxxxxxx", "Stack high water mark: %u bytes remaining", high_water_mark * sizeof(StackType_t));
        ESP_LOGI(TAG, "TTS request received: %s", text);
        g_audio_playing = true; // Set audio playing flag

        tts_send_text(text);    // Send text to TTS service
        free(text);             // Free text after sending to TTS
        text = NULL;

        uint8_t *data = NULL;
        g_face_updated = true;

        while (1) {
            size_t len = 0;
            size_t total_len = 0;

            if (tts_recv_audio(&data, &len, &total_len)) { // Receive audio data from TTS service
                if (data == NULL) {
                    ESP_LOGE(TAG, "TTS audio data received is NULL");
                    break; // Exit loop if NULL data received from TTS
                }
                else {
                    ESP_LOGV(TAG, "Received TTS audio chunk: len=%d, total_len=%d", len, total_len);
                    if (g_face_updated) {
                        //ui_send_sys_event(ui_face, LV_EVENT_FACE_SPEAK, NULL); // UI event, keep as is
                        g_face_updated = false;
                    }
                    if (audio_play(data, len) != ESP_OK) { // Enqueue audio chunk for playback
                        ESP_LOGE(TAG, "Failed to enqueue TTS audio chunk for playback");
                    }
                    free(data); // Free audio data chunk after enqueuing for playback
                    data = NULL;
                }
            }
            else {
                ESP_LOGI(TAG, "TTS audio stream finished");
                break; // Exit loop when TTS audio stream is complete
            }
        }

        g_audio_playing = false; // Reset audio playing flag after TTS playback completion

        vTaskDelay(300); // Short delay after playback (consider making delay configurable if needed)
        if (!g_audio_playing) {
            //ui_send_sys_event(ui_face, LV_EVENT_FACE_LOOK, NULL); // UI event, keep as is after playback.
        }

        // Log heap memory stats after audio play for debugging
        ESP_LOGI(TAG, "Heap after audio play, internal current: %d, minimum: %d, total current: %d, minimum: %d",
            heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL),
            heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL),
            (int)esp_get_free_heap_size(),
            (int)esp_get_minimum_free_heap_size());
    }

    ESP_LOGI(TAG, "audio_tts_task end"); // Should not reach here in normal continuous operation
    vTaskDelete(NULL);
}

/**
 * @brief Audio play task to dequeue and play audio data using codec device.
 *
 * @param arg Unused argument.
 */
static void audio_play_task(void *arg)
{
    spk_codec_dev = audio_codec_dev; // Initialize speaker codec device

    audio_data_t audio_data = { 0 };
    while (xQueueReceive(g_queue_audio_play, &audio_data, portMAX_DELAY) == pdTRUE) {
        high_water_mark = uxTaskGetStackHighWaterMark(NULL);
        //ESP_LOGI("audio_play_task   xxxxxxxxxxxxxxx", "Stack high water mark: %u bytes remaining", high_water_mark * sizeof(StackType_t));
        ESP_LOGV(TAG, "Audio play task dequeued data, len=%d, wav=%p", audio_data.len, audio_data.wav);
        // ESP_LOGI(TAG, "Audio playback started");
        if (esp_codec_dev_write(spk_codec_dev, audio_data.wav, audio_data.len) != ESP_OK) { // Write audio data to codec for playback
            ESP_LOGE(TAG, "Error writing audio data to codec device for playback");
        }
        free(audio_data.wav); // Free audio data buffer after playback
        audio_data.wav = NULL; // Set to NULL to avoid double free if any logic error occurs
        //ESP_LOGI(TAG, "Audio playback finished");
    }

    // esp_codec_dev_close(spk_codec_dev); // Codec close - consider when and where to close codec device based on application lifecycle
    vTaskDelete(NULL); // Should not reach here in normal continuous operation
}

/**
 * @brief Initialize audio subsystem, AFE-SR, and create audio processing tasks.
 *
 * @return esp_err_t ESP_OK if initialization successful, ESP_FAIL otherwise.
 */
esp_err_t app_sr_init(void)
{
    ESP_LOGI(TAG, "Starting audio subsystem initialization");

    // 分配音频记录缓冲区
    g_audio_record_buf = heap_caps_malloc(MAX_AUDIO_INPUT_LENGTH + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(g_audio_record_buf != NULL, ESP_ERR_NO_MEM, TAG, "Failed to allocate audio record buffer");
    ESP_LOGD(TAG, "Audio record buffer allocated, size=%d", MAX_AUDIO_INPUT_LENGTH + 1);

    // 初始化语音识别模型
    models = esp_srmodel_init("model"); // Model name needs to match partition table entry
    ESP_RETURN_ON_FALSE(models != NULL, ESP_FAIL, TAG, "Failed to initialize SR model");
    ESP_LOGD(TAG, "SR model initialized");

    // 初始化 AFE-SR （声学前端语音识别）
    afe_handle = (esp_afe_sr_iface_t *)&ESP_AFE_SR_HANDLE; // Get AFE-SR handle
    afe_config_t afe_config = AFE_CONFIG_DEFAULT();          // Load default AFE configuration
    afe_config.pcm_config.mic_num = 1;                      // Set microphone number to 1 (mono)
    afe_config.pcm_config.total_ch_num = 2;   // Set total channels based on I2S_CHANNEL_NUM, for output config maybe stereo 2.
    afe_config.wakenet_model_name = esp_srmodel_filter(models, ESP_WN_PREFIX, NULL); // Filter and set wake word model
    afe_data = afe_handle->create_from_config(&afe_config); // Create AFE-SR data from config
    ESP_RETURN_ON_FALSE(afe_data != NULL, ESP_FAIL, TAG, "Failed to create AFE-SR data");
    ESP_LOGI(TAG, "Wakenet model: %s", afe_config.wakenet_model_name);

    // 创建用于任务间通信的队列
    g_result_que = xQueueCreate(1, sizeof(audio_record_state_t));
    ESP_RETURN_ON_FALSE(g_result_que != NULL, ESP_ERR_NO_MEM, TAG, "Failed to create result queue");
    g_audio_chat_queue = xQueueCreate(16, sizeof(char *));
    ESP_RETURN_ON_FALSE(g_audio_chat_queue != NULL, ESP_ERR_NO_MEM, TAG, "Failed to create audio chat queue");
    g_stt_event_group = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(g_stt_event_group != NULL, ESP_ERR_NO_MEM, TAG, "Failed to create STT event group");
    g_audio_tts_queue = xQueueCreate(16, sizeof(char *));
    ESP_RETURN_ON_FALSE(g_audio_tts_queue != NULL, ESP_ERR_NO_MEM, TAG, "Failed to create audio TTS queue");
    g_queue_audio_play = xQueueCreate(1, sizeof(audio_data_t));
    ESP_RETURN_ON_FALSE(g_queue_audio_play != NULL, ESP_ERR_NO_MEM, TAG, "Failed to create audio play queue");
    ESP_LOGD(TAG, "Queues created");

    // Create tasks for audio processing, pinned to cores for potential performance reasons (review core assignments)
    BaseType_t ret_val;
    ret_val = xTaskCreatePinnedToCore(audio_feed_task, "Feed Task", 2.5 * 1024, afe_data, 5, &xFeedHandle, 0); // Core 0
    ESP_RETURN_ON_FALSE(ret_val == pdPASS, ESP_FAIL, TAG, "Failed to create audio feed task");
    ret_val = xTaskCreatePinnedToCore(audio_detect_task, "Detect Task", 4 * 1024, afe_data, 5, &xDetectHandle, 1); // Core 1
    ESP_RETURN_ON_FALSE(ret_val == pdPASS, ESP_FAIL, TAG, "Failed to create audio detect task");
    ret_val = xTaskCreatePinnedToCore(audio_record_task, "Audio Record Task", 4 * 1024, g_result_que, 1, NULL, 0); // Core 0 - less priority
    ESP_RETURN_ON_FALSE(ret_val == pdPASS, ESP_FAIL, TAG, "Failed to create audio handler task");
    ret_val = xTaskCreatePinnedToCore(app_stt_task, "audio_stt", 1024 * 4, NULL, 2, NULL, 1); // Core 1 - STT task
    ESP_RETURN_ON_FALSE(ret_val == pdPASS, ESP_FAIL, TAG, "Failed to create STT task");
    ret_val = xTaskCreatePinnedToCore(audio_chat_task, "audio_chat", 1024 * 6, NULL, 1, NULL, 0); // Core 0 - Chat task
    ESP_RETURN_ON_FALSE(ret_val == pdPASS, ESP_FAIL, TAG, "Failed to create audio chat task");
    ret_val = xTaskCreatePinnedToCore(audio_tts_task, "audio_tts", 1024 * 6, NULL, 6, NULL, 1); // Core 1 - TTS task
    ESP_RETURN_ON_FALSE(ret_val == pdPASS, ESP_FAIL, TAG, "Failed to create audio TTS task");
    ret_val = xTaskCreate(audio_play_task, "audio_play_task", 1024 * 5, NULL, 15, NULL);           // No core pinning - Audio play task
    ESP_RETURN_ON_FALSE(ret_val == pdPASS, ESP_FAIL, TAG, "Failed to create audio play task");
    ESP_LOGI(TAG, "Audio tasks created");

    ESP_LOGI(TAG, "Audio subsystem initialization completed successfully");
    return ESP_OK;
}